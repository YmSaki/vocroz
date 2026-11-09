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
#include "vocroz/transport/noise_ik_handshake.h"
#include "vocroz/transport/packet_header.h"
#include "vocroz/transport/ports/control_message_listener.h"
#include "vocroz/transport/ports/liveness_listener.h"
#include "vocroz/transport/reliable_control_channel.h"
#include "vocroz/transport/secure_control_packet_sink.h"

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

// 1 つの接続（子機 1 台との暗号化セッション）。送信方向と信頼制御層はコントロールスレッドが、
// receive は受信スレッドが使う。
struct HostPeerLinks::Peer final : transport::ControlMessageListener, transport::LivenessListener {
    Peer(HostPeerLinks& owner_in, session::PeerHandle handle_in, const network::UdpAddress& from,
         transport::SecureChannel secure, std::shared_ptr<PlayoutSource> source_in,
         std::span<const std::uint8_t> initiation_in, std::vector<std::uint8_t> response_in)
        : owner(owner_in),
          handle(handle_in),
          address(from),
          socket(owner_in.ports_.endpoint, from),
          sealer(std::move(secure.sealer)),
          sink(sealer, socket),
          control(owner_in.ports_.clock, sink, *this),
          liveness(owner_in.ports_.clock, *this),
          source(std::move(source_in)),
          receive(handle_in, std::move(secure.opener), from, liveness, source->stream(), socket,
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
    network::PeerDatagramSocket socket;
    transport::PacketSealer sealer;
    transport::SecureControlPacketSink sink;
    transport::ReliableControlChannel control;
    transport::LivenessMonitor liveness;
    // ジッターバッファ。ストリップにつないでいる間はミキサーのグラフも参照する。
    std::shared_ptr<PlayoutSource> source;
    ReceivePeer receive;
    // 送り直された同じ 1 つ目のメッセージには、同じ応答を返し直す。
    std::vector<std::uint8_t> initiation;
    std::vector<std::uint8_t> response;
    // admit() で決まるストリップ。
    std::optional<std::string> strip;
    // 受信スレッドに取り外しを指示した。on_receive_released() で破棄する。
    bool closing = false;
};

HostPeerLinks::HostPeerLinks(Ports ports, const transport::PlayoutConfig& playout)
    : ports_(ports),
      playout_(playout),
      wake_address_(network::UdpAddress::from_ipv4({127, 0, 0, 1}, ports.endpoint.local_port())) {}

HostPeerLinks::~HostPeerLinks() = default;

void HostPeerLinks::poll() {
    flush_commands();
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

void HostPeerLinks::admit(session::PeerHandle peer, const std::string& strip_id) {
    Peer* const found = find(peer);
    if (found == nullptr) {
        return;
    }
    if (!ports_.mixer.attach_source(strip_id, found->source)) {
        ports_.logger.log(common::LogLevel::kError,
                          "子機の受信ストリームをストリップにつなげませんでした");
        defer([&events = ports_.events, peer] { events.on_peer_path_failed(peer); });
        return;
    }
    if (found->strip && *found->strip != strip_id) {
        const auto owner = strip_owners_.find(*found->strip);
        if (owner != strip_owners_.end() && owner->second == peer) {
            ports_.mixer.detach_source(*found->strip);
            strip_owners_.erase(owner);
        }
    }
    found->strip = strip_id;
    strip_owners_[strip_id] = peer;
    command(ReceiveCommand{.kind = ReceiveCommand::Kind::kAdmit, .peer = &found->receive});
    // ジッターバッファをグラフに組み込み、受信スレッドへの指示を積んでから知らせる。
    // 指示は次のデータグラムの前に反映されるので、以後に届くメディアはストリップへ流れる。
    defer([&events = ports_.events, peer] { events.on_peer_established(peer); });
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
    if (Peer* const found = find(peer); found != nullptr && found->strip) {
        defer([&events = ports_.events, peer, state] {
            events.on_signal_state(peer, to_protocol(state));
        });
    }
    dispatch_events();
}

void HostPeerLinks::on_receive_released(session::PeerHandle peer) {
    // 受信スレッドがもう参照しないので、復号器とジッターバッファを破棄してよい。
    // ジッターバッファはミキサーの古いグラフが参照していれば、その破棄で外れる。
    peers_.erase(peer);
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
    auto stream = transport::PlayoutStream::create(playout_);
    if (!stream) {
        ports_.logger.log(common::LogLevel::kError, "子機のジッターバッファを作れませんでした");
        return;
    }
    for (auto& [handle, peer] : peers_) {
        if (!peer->closing && !peer->strip && peer->address == from) {
            // 承認前の接続を同じ送信元が張り直した。戻る先がないので、申請ごと閉じる。
            // 承認済みの接続は、再接続の申請で session が close() するまで残す。ここで閉じたと
            // 知らせると、session は子機が退出したとみなしてストリップを片付けてしまうため。
            begin_close(*peer);
            defer([&events = ports_.events, old = handle] { events.on_peer_closed(old); });
        }
    }
    const auto handle = next_handle_++;
    auto source = std::make_shared<PlayoutSource>(std::move(stream).value());
    auto [entry, inserted] = peers_.emplace(
        handle, std::make_unique<Peer>(*this, handle, from, std::move(secure).value(),
                                       std::move(source), datagram, std::move(response)));
    Peer& peer = *entry->second;
    command(ReceiveCommand{.kind = ReceiveCommand::Kind::kAttach, .peer = &peer.receive});
    static_cast<void>(ports_.endpoint.send_to(from, peer.response));
    // 子機の 1 つ目のメッセージが届いた経路が開いたので、その候補へのホールパンチは要らない。
    ports_.rendezvous.mark_established(from);
}

void HostPeerLinks::begin_close(Peer& peer) {
    peer.closing = true;
    if (peer.strip) {
        const auto owner = strip_owners_.find(*peer.strip);
        if (owner != strip_owners_.end() && owner->second == peer.handle) {
            ports_.mixer.detach_source(*peer.strip);
            strip_owners_.erase(owner);
        }
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
