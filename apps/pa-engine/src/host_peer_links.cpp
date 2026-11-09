// SPDX-License-Identifier: AGPL-3.0-only
//
// HostPeerLinks の実装。

#include "host_peer_links.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

#include "vocroz/network/peer_datagram_socket.h"
#include "vocroz/transport/constants.h"
#include "vocroz/transport/control_lane.h"
#include "vocroz/transport/liveness_monitor.h"
#include "vocroz/transport/media_stream_encoder.h"
#include "vocroz/transport/noise_ik_handshake.h"
#include "vocroz/transport/opus_frame_encoder.h"
#include "vocroz/transport/packet_header.h"
#include "vocroz/transport/peer_packet_sender.h"
#include "vocroz/transport/ports/control_message_listener.h"
#include "vocroz/transport/ports/liveness_listener.h"
#include "vocroz/transport/reliable_control_channel.h"

#include "playout_source.h"

namespace vocroz::engine {
namespace {

namespace cp = control_protocol;

// 受信スレッドを起こすデータグラム。ランデブーメッセージにも子機のパケットのヘッダーにも
// ならない長さ（ReceiveRouter は空のデータグラムを捨てるので 1 バイトにする）。
constexpr std::array<std::uint8_t, 1> kWakeDatagram{0};

cp::SignalState to_protocol(transport::SignalState state) {
    switch (state) {
        case transport::SignalState::kReceiving:
            return cp::SignalState::kReceiving;
        case transport::SignalState::kPaused:
            return cp::SignalState::kPaused;
        case transport::SignalState::kResuming:
            return cp::SignalState::kResuming;
        case transport::SignalState::kNoSignal:
            return cp::SignalState::kNoSignal;
    }
    return cp::SignalState::kNoSignal;
}

}  // namespace

// 1 つの接続の送信方向。制御パケットと返送のメディアを送信待ちに積み、送信スレッドが暗号化して
// 送る。返送のエンコーダーはミキサーのグラフからも参照されるので、Peer とは別に共有で持つ。
struct HostPeerLinks::Outbound {
    Outbound(network::UdpEndpoint& endpoint, const network::UdpAddress& to,
             transport::PacketSealer sealer, std::unique_ptr<PeerSendWorker> worker_in)
        : socket(endpoint, to),
          worker(std::move(worker_in)),
          packets(std::move(sealer), socket, transport::kReturnStreamId, *worker) {
        worker->start(packets);
    }
    Outbound(const Outbound&) = delete;
    Outbound& operator=(const Outbound&) = delete;
    // packets を破棄する前に、それを回す送信スレッドを止める。
    ~Outbound() {
        worker->stop();
    }

    network::PeerDatagramSocket socket;
    std::unique_ptr<PeerSendWorker> worker;
    transport::PeerPacketSender packets;
    // 返送（Minus-Me）のエンコーダー。作れなかったら空で、返送は送らない。初期値はミュートなので、
    // session がミュートを解くまで何も送らない（docs/adr/0036-return-monitor.md）。
    std::optional<transport::MediaStreamEncoder> encoder;
};

// 1 つの接続（子機 1 台との暗号化セッション）。信頼制御層はコントロールスレッドが、
// receive は受信スレッドが、outbound の送信待ちの取り出しは送信スレッドが使う。
struct HostPeerLinks::Peer final : transport::ControlMessageListener, transport::LivenessListener {
    // 1 本のストリーム（子機の入力 1 つ）。
    struct Stream {
        std::uint16_t stream_id = 0;
        std::uint8_t channels = 1;
        std::shared_ptr<PlayoutSource> source;
    };

    Peer(HostPeerLinks& owner_in, session::PeerHandle handle_in, const network::UdpAddress& from,
         transport::SecureChannel secure, std::span<const std::uint8_t> initiation_in,
         std::vector<std::uint8_t> response_in)
        : owner(owner_in),
          handle(handle_in),
          address(from),
          outbound(std::make_shared<Outbound>(owner_in.ports_.endpoint, from,
                                              std::move(secure.sealer),
                                              owner_in.ports_.sending.create())),
          control(owner_in.ports_.clock, outbound->packets, *this),
          liveness(owner_in.ports_.clock, *this),
          receive(handle_in, std::move(secure.opener), from, liveness, outbound->socket,
                  owner_in.ports_.clock.now()),
          initiation(initiation_in.begin(), initiation_in.end()),
          response(std::move(response_in)) {}

    void on_control_message(transport::ControlLane lane,
                            std::span<const std::uint8_t> message) override {
        // MIDI のレーンは、子機の MIDI の受け口がつながってから扱う
        // （docs/adr/0029-midi-and-vsti.md）。
        if (lane != transport::ControlLane::kControl || closing) {
            return;
        }
        owner.defer([&events = owner.ports_.events, peer = handle,
                     bytes = std::vector<std::uint8_t>(message.begin(), message.end())] {
            events.on_peer_message(peer, bytes);
        });
    }

    void on_peer_timed_out() override {
        owner.defer(
            [&events = owner.ports_.events, peer = handle] { events.on_peer_timeout(peer); });
    }

    void on_peer_resumed() override {
        owner.defer(
            [&events = owner.ports_.events, peer = handle] { events.on_peer_resumed(peer); });
    }

    HostPeerLinks& owner;
    session::PeerHandle handle;
    // 鍵交換の 1 つ目のメッセージの送信元。同じ送信元からの申請の張り直しを見分けるのに使う。
    network::UdpAddress address;
    std::shared_ptr<Outbound> outbound;
    transport::ReliableControlChannel control;
    transport::LivenessMonitor liveness;
    ReceivePeer receive;
    // 送り直された同じ 1 つ目のメッセージには、同じ応答を返し直す。
    std::vector<std::uint8_t> initiation;
    std::vector<std::uint8_t> response;
    // 今の対応のストリーム。ジッターバッファはストリップにつないでいる間、ミキサーのグラフも
    // 参照する。
    std::vector<Stream> streams;
    // 受信スレッドに渡した表と、対応から外したジッターバッファ。受信スレッドがまだ参照している
    // かもしれないので、取り外しを反映したと知らされるまで保つ。対応の変更はまれなので、
    // 増え方は小さい。
    std::vector<std::unique_ptr<StreamTable>> tables;
    std::vector<std::shared_ptr<PlayoutSource>> retired;
    // この接続がつないでいるストリップと、つないだジッターバッファ。
    std::map<std::string, PlayoutSource*, std::less<>> attached;
    // session が承認した（admit() が呼ばれた）。
    bool admitted = false;
    // on_peer_established() か on_peer_path_failed() を知らせた。
    bool reported_admission = false;
    // 受信スレッドに取り外しを指示した。on_receive_released() で破棄する。
    bool closing = false;
    // 返送のエンコーダーをミキサーの返送につないでいる。
    bool return_attached = false;
};

HostPeerLinks::HostPeerLinks(Ports ports, const transport::PlayoutConfig& playout)
    : ports_(ports),
      playout_(playout),
      wake_address_(network::UdpAddress::from_ipv4({127, 0, 0, 1}, ports.endpoint.local_port())) {}

HostPeerLinks::~HostPeerLinks() = default;

void HostPeerLinks::stop_sending() {
    for (auto& [handle, peer] : peers_) {
        peer->outbound->worker->stop();
    }
    for (auto& outbound : released_outbound_) {
        outbound->worker->stop();
    }
}

void HostPeerLinks::poll() {
    flush_commands();
    // ミキサーの古いグラフが外れて参照がここだけになった送信方向を、送信スレッドごと破棄する。
    std::erase_if(released_outbound_, [](const std::shared_ptr<Outbound>& outbound) {
        return outbound.use_count() == 1;
    });
    const auto now = ports_.clock.now();
    for (auto& [handle, peer] : peers_) {
        if (peer->closing) {
            continue;
        }
        if (now >= peer->control.next_deadline()) {
            peer->control.tick();
        }
        peer->liveness.poll();
    }
    // 接続がなければ、受信スレッドに反映する指示も判定する信号状態もない。
    if (!peers_.empty() && now >= next_wake_) {
        next_wake_ = now + kReceiveWakeInterval;
        // 届かなくても次の周期で送り直すので、失敗は無視する。
        static_cast<void>(ports_.endpoint.send_to(wake_address_, kWakeDatagram));
    }
    dispatch_events();
}

std::size_t HostPeerLinks::peer_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        peers_.begin(), peers_.end(), [](const auto& entry) { return !entry.second->closing; }));
}

void HostPeerLinks::send(session::PeerHandle peer, std::span<const std::uint8_t> message) {
    Peer* const found = find(peer);
    if (found == nullptr) {
        return;
    }
    if (!found->control.send(transport::ControlLane::kControl, message)) {
        ports_.logger.log(common::LogLevel::kWarning,
                          "子機への制御メッセージを送信待ちに積めませんでした");
    }
}

void HostPeerLinks::admit(session::PeerHandle peer,
                          const std::vector<session::StreamRoute>& routes) {
    Peer* const found = find(peer);
    if (found == nullptr || routes.empty()) {
        return;
    }
    found->admitted = true;
    const bool connected = apply_routes(*found, routes);
    attach_return(*found, routes);
    if (found->reported_admission) {
        // 再接続や申告の変更での置き換え。接続はもう確立しているので、失敗は記録だけにする。
        return;
    }
    found->reported_admission = true;
    if (!connected) {
        defer([&events = ports_.events, peer] { events.on_peer_path_failed(peer); });
        return;
    }
    // ジッターバッファをグラフに組み込み、受信スレッドへの指示を積んでから知らせる。
    // 指示は次のデータグラムの前に反映されるので、以後に届くメディアはストリップへ流れる。
    defer([&events = ports_.events, peer] { events.on_peer_established(peer); });
}

bool HostPeerLinks::apply_routes(Peer& peer, const std::vector<session::StreamRoute>& routes) {
    bool connected = true;
    std::vector<Peer::Stream> next;
    auto table = std::make_unique<StreamTable>();
    // ストリップごとに、つなぐジッターバッファ。
    std::map<std::string, std::shared_ptr<PlayoutSource>, std::less<>> wanted;
    for (const auto& route : routes) {
        if (table->count >= table->slots.size()) {
            // session は申告を kMaxInputStreams 本までに検証しているので、ここには来ない。
            connected = false;
            break;
        }
        // 同じストリームを同じチャンネル数のまま流し続けるなら、ジッターバッファを作り直さない
        // （溜めた音を捨てて途切れさせないため）。
        const auto same = std::find_if(
            peer.streams.begin(), peer.streams.end(), [&route](const Peer::Stream& stream) {
                return stream.stream_id == route.stream_id && stream.channels == route.channels;
            });
        std::shared_ptr<PlayoutSource> source;
        if (same != peer.streams.end()) {
            source = same->source;
        } else {
            auto config = playout_;
            config.channels = route.channels;
            auto stream = transport::PlayoutStream::create(config);
            if (!stream) {
                ports_.logger.log(common::LogLevel::kError,
                                  "子機のジッターバッファを作れませんでした");
                connected = false;
                continue;
            }
            source = std::make_shared<PlayoutSource>(std::move(stream).value());
        }
        table->slots[table->count++] = StreamTable::Slot{route.stream_id, &source->stream()};
        wanted[route.strip_id] = source;
        next.push_back(Peer::Stream{route.stream_id, route.channels, std::move(source)});
    }
    // 対応から外れたストリップを先に外し、同じストリップへ別のストリームをつなぎ直せるようにする。
    std::vector<std::string> stale;
    for (const auto& [strip, source] : peer.attached) {
        const auto want = wanted.find(strip);
        if (want == wanted.end() || want->second.get() != source) {
            stale.push_back(strip);
        }
    }
    for (const auto& strip : stale) {
        detach_strip(peer, strip);
    }
    for (const auto& [strip, source] : wanted) {
        if (peer.attached.contains(strip)) {
            continue;
        }
        if (!ports_.mixer.attach_source(strip, source)) {
            ports_.logger.log(common::LogLevel::kError,
                              "子機の受信ストリームをストリップにつなげませんでした");
            connected = false;
            continue;
        }
        peer.attached[strip] = source.get();
        strip_owners_[strip] = peer.handle;
    }
    for (auto& old : peer.streams) {
        if (std::none_of(next.begin(), next.end(), [&old](const Peer::Stream& stream) {
                return stream.source == old.source;
            })) {
            peer.retired.push_back(std::move(old.source));
        }
    }
    peer.streams = std::move(next);
    command(ReceiveCommand{
        .kind = ReceiveCommand::Kind::kAdmit, .peer = &peer.receive, .streams = table.get()});
    peer.tables.push_back(std::move(table));
    return connected;
}

void HostPeerLinks::attach_return(Peer& peer, const std::vector<session::StreamRoute>& routes) {
    auto& encoder = peer.outbound->encoder;
    if (!encoder) {
        return;
    }
    // Minus-Me は、この子機の入力をつないだストリップをすべて抜く。同じキー（接続）でつなぎ直すと
    // 前の除外が置き換わるので、入力の申告が変わっても新しい対応で抜き直せる。
    std::vector<std::string> own_strips;
    for (const auto& route : routes) {
        if (std::find(own_strips.begin(), own_strips.end(), route.strip_id) == own_strips.end()) {
            own_strips.push_back(route.strip_id);
        }
    }
    // エンコーダーは送信方向（Outbound）の一部として生かす。グラフが参照している間は、
    // 接続を破棄しても送信方向は残る。
    std::shared_ptr<audio_core::AudioSink> sink(peer.outbound, &*encoder);
    if (ports_.mixer.attach_return(peer.handle, std::move(own_strips), std::move(sink))) {
        peer.return_attached = true;
    } else {
        ports_.logger.log(common::LogLevel::kError, "子機への返送をミキサーにつなげませんでした");
    }
}

void HostPeerLinks::detach_strip(Peer& peer, const std::string& strip_id) {
    const auto owner = strip_owners_.find(strip_id);
    if (owner != strip_owners_.end() && owner->second == peer.handle) {
        // 再接続では新しい接続が先に同じストリップへつながるので、自分がつないでいるときだけ外す。
        ports_.mixer.detach_source(strip_id);
        strip_owners_.erase(owner);
    }
    peer.attached.erase(strip_id);
}

void HostPeerLinks::apply_send_mute(session::PeerHandle peer, bool send_muted,
                                    std::uint32_t epoch) {
    if (Peer* const found = find(peer)) {
        command(ReceiveCommand{.kind = ReceiveCommand::Kind::kSendMute,
                               .peer = &found->receive,
                               .send_muted = send_muted,
                               .mute_epoch = epoch});
    }
}

void HostPeerLinks::set_return_mute(session::PeerHandle peer, bool muted, std::uint32_t epoch) {
    Peer* const found = find(peer);
    if (found == nullptr || !found->outbound->encoder) {
        return;
    }
    // 返送ごとの音量は持たないので、送信レベルは 1 のまま。
    if (!found->outbound->encoder->set_params(
            transport::StreamSendParams{.muted = muted, .mute_epoch = epoch, .gain = 1.0F})) {
        // オーディオスレッドが止まっていて設定が溜まりきった。エンコーダーは最後に受け取った
        // 設定のまま（初期値はミュート）なので、記録だけにする。
        ports_.logger.log(common::LogLevel::kWarning, "子機への返送の設定を渡せませんでした");
    }
}

void HostPeerLinks::close(session::PeerHandle peer) {
    if (Peer* const found = find(peer)) {
        begin_close(*found);
    }
}

void HostPeerLinks::on_handshake_initiation(const network::UdpAddress& from,
                                            std::span<const std::uint8_t> datagram) {
    accept(from, datagram);
    dispatch_events();
}

void HostPeerLinks::on_control_packet(session::PeerHandle peer,
                                      std::span<const std::uint8_t> plaintext) {
    if (Peer* const found = find(peer)) {
        if (!found->control.on_control_packet(plaintext)) {
            ports_.logger.log(common::LogLevel::kDebug, "形式の誤った制御パケットを捨てました");
        }
    }
    dispatch_events();
}

void HostPeerLinks::on_signal_state(session::PeerHandle peer, transport::SignalState state) {
    if (Peer* const found = find(peer); found != nullptr && found->admitted) {
        defer([&events = ports_.events, peer, state] {
            events.on_signal_state(peer, to_protocol(state));
        });
    }
    dispatch_events();
}

void HostPeerLinks::on_receive_released(session::PeerHandle peer) {
    // 受信スレッドがもう参照しないので、復号器とジッターバッファを破棄してよい。
    // ジッターバッファはミキサーの古いグラフが参照していれば、その破棄で外れる。
    // 送信方向は、グラフの参照が外れるまで released_outbound_ で保つ。
    if (const auto found = peers_.find(peer); found != peers_.end()) {
        released_outbound_.push_back(found->second->outbound);
        peers_.erase(found);
    }
    dispatch_events();
}

HostPeerLinks::Peer* HostPeerLinks::find(session::PeerHandle handle) {
    const auto found = peers_.find(handle);
    return found == peers_.end() || found->second->closing ? nullptr : found->second.get();
}

HostPeerLinks::Peer* HostPeerLinks::find_by_connection(std::uint32_t connection_id) {
    for (auto& [handle, peer] : peers_) {
        if (!peer->closing && peer->receive.opener.connection_id() == connection_id) {
            return peer.get();
        }
    }
    return nullptr;
}

void HostPeerLinks::accept(const network::UdpAddress& from,
                           std::span<const std::uint8_t> datagram) {
    const auto header = transport::parse_packet_header(datagram);
    if (!header) {
        return;
    }
    if (Peer* const existing = find_by_connection(header.value().connection_id)) {
        // 応答が失われたか、別の経路から同じメッセージが届いた。同じ応答を返し直す。
        // 中身が違うものは偽造か接続 ID の衝突なので、既存の接続を守って捨てる。
        if (std::equal(datagram.begin(), datagram.end(), existing->initiation.begin(),
                       existing->initiation.end())) {
            static_cast<void>(ports_.endpoint.send_to(from, existing->response));
        }
        return;
    }
    if (peers_.size() >= kMaxPeerConnections) {
        ports_.logger.log(common::LogLevel::kWarning,
                          "接続が上限に達しているため、子機の鍵交換を断りました");
        return;
    }
    auto responder = transport::NoiseIkHandshake::create_responder(ports_.keys);
    if (!responder) {
        return;
    }
    std::vector<std::uint8_t> payload(datagram.size());
    if (!responder.value().read_initiation(datagram, payload)) {
        // 親機の公開鍵を知らない送信元か、壊れたメッセージ。
        return;
    }
    std::vector<std::uint8_t> response(transport::kMaxPacketBytes);
    const auto written = responder.value().write_response({}, response);
    if (!written) {
        return;
    }
    response.resize(written.value());
    auto secure = responder.value().finish();
    if (!secure) {
        return;
    }
    for (auto& [handle, peer] : peers_) {
        if (!peer->closing && !peer->admitted && peer->address == from) {
            // 承認前の接続を同じ送信元が張り直した。戻る先がないので、申請ごと閉じる。
            // 承認済みの接続は、再接続の申請で session が close() するまで残す。ここで閉じたと
            // 知らせると、session は子機が退出したとみなしてストリップを片付けてしまうため。
            begin_close(*peer);
            defer([&events = ports_.events, old = handle] { events.on_peer_closed(old); });
        }
    }
    const auto handle = next_handle_++;
    auto [entry, inserted] = peers_.emplace(
        handle, std::make_unique<Peer>(*this, handle, from, std::move(secure).value(), datagram,
                                       std::move(response)));
    Peer& peer = *entry->second;
    create_return_encoder(peer);
    command(ReceiveCommand{.kind = ReceiveCommand::Kind::kAttach, .peer = &peer.receive});
    static_cast<void>(ports_.endpoint.send_to(from, peer.response));
    // 子機の 1 つ目のメッセージが届いた経路が開いたので、その候補へのホールパンチは要らない。
    ports_.rendezvous.mark_established(from);
}

void HostPeerLinks::create_return_encoder(Peer& peer) {
    // 返送はミキサーのモノラルのバス（Minus-Me）なので、モノラルでエンコードする。
    auto opus = transport::OpusFrameEncoder::create(transport::OpusEncoderConfig{});
    if (!opus) {
        // 返送がなくても子機の音は親機に届くので、接続は続ける。
        ports_.logger.log(common::LogLevel::kError, "子機への返送のエンコーダーを作れませんでした");
        return;
    }
    peer.outbound->encoder.emplace(std::move(opus).value(), peer.outbound->packets);
}

void HostPeerLinks::begin_close(Peer& peer) {
    peer.closing = true;
    std::vector<std::string> strips;
    for (const auto& [strip, source] : peer.attached) {
        strips.push_back(strip);
    }
    for (const auto& strip : strips) {
        detach_strip(peer, strip);
    }
    if (peer.return_attached) {
        ports_.mixer.detach_return(peer.handle);
        peer.return_attached = false;
    }
    command(ReceiveCommand{.kind = ReceiveCommand::Kind::kDetach, .peer = &peer.receive});
}

void HostPeerLinks::command(const ReceiveCommand& command) {
    if (!pending_commands_.empty() || !ports_.receive.push(command)) {
        pending_commands_.push_back(command);
    }
}

void HostPeerLinks::flush_commands() {
    while (!pending_commands_.empty() && ports_.receive.push(pending_commands_.front())) {
        pending_commands_.pop_front();
    }
}

void HostPeerLinks::defer(std::function<void()> action) {
    events_.push_back(std::move(action));
}

void HostPeerLinks::dispatch_events() {
    // 通知の中で session が PeerLink を呼ぶと、新しい通知が溜まることがある。
    // 空になるまで繰り返す。
    while (!events_.empty()) {
        auto actions = std::move(events_);
        events_.clear();
        for (auto& action : actions) {
            action();
        }
    }
}

}  // namespace vocroz::engine
