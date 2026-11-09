// SPDX-License-Identifier: AGPL-3.0-only
//
// SimulatedHost の実装。

#include "vocroz/test_harness/simulated_host.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <utility>

#include "vocroz/test_harness/media_receiver.h"
#include "vocroz/test_harness/simulated_datagram_socket.h"
#include "vocroz/transport/constants.h"
#include "vocroz/transport/control_lane.h"
#include "vocroz/transport/liveness_monitor.h"
#include "vocroz/transport/media_payload.h"
#include "vocroz/transport/noise_ik_handshake.h"
#include "vocroz/transport/packet_header.h"
#include "vocroz/transport/ports/control_message_listener.h"
#include "vocroz/transport/ports/liveness_listener.h"
#include "vocroz/transport/reliable_control_channel.h"
#include "vocroz/transport/secure_control_packet_sink.h"
#include "vocroz/transport/signal_state.h"

namespace vocroz::test_harness {
namespace {

namespace cp = control_protocol;

// session の tick() の間隔。session の README の推奨（100 ms〜1 s）の短い側にして、
// 招待と再接続の猶予の判定の遅れを小さくする。
constexpr std::chrono::milliseconds kSessionTickInterval{100};

transport::StaticKeyPair generate_keys() {
    auto keys = transport::StaticKeyPair::generate();
    if (!keys) {
        // 鍵を作れない環境では結合テストを続けられない。
        std::abort();
    }
    return std::move(keys).value();
}

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

// 1 つの接続（子機 1 台との暗号化セッション）。
// 1 本のストリームの流し込み先。
struct SimulatedHost::Route {
    std::uint16_t stream_id = 0;
    std::uint8_t channels = 1;
    std::string strip_id;
    std::unique_ptr<MediaReceiver> receiver;
};

struct SimulatedHost::Peer final : transport::ControlMessageListener, transport::LivenessListener {
    Peer(SimulatedHost& owner, session::PeerHandle handle, const netsim::NodeAddress& address,
         transport::SecureChannel secure, std::span<const std::uint8_t> initiation,
         std::span<const std::uint8_t> response)
        : host(owner),
          handle(handle),
          address(address),
          channel(std::move(secure)),
          socket(owner.network_, owner.address_, address),
          sink(channel.sealer, socket),
          control(owner.clock_, sink, *this),
          liveness(owner.clock_, *this),
          initiation(initiation.begin(), initiation.end()),
          response(response.begin(), response.end()),
          plaintext(transport::kMaxPacketBytes) {}

    // ストリーム ID の流し込み先。なければ nullptr。
    Route* route(std::uint16_t stream_id) {
        const auto it =
            std::ranges::find_if(routes, [&](const Route& r) { return r.stream_id == stream_id; });
        return it == routes.end() ? nullptr : &*it;
    }

    void on_control_message(transport::ControlLane lane,
                            std::span<const std::uint8_t> message) override {
        if (lane == transport::ControlLane::kControl && !closed) {
            host.session_->on_peer_message(handle, message);
        }
    }

    void on_peer_timed_out() override {
        const auto peer = handle;
        host.defer([this_host = &host, peer] { this_host->session_->on_peer_timeout(peer); });
    }

    void on_peer_resumed() override {
        const auto peer = handle;
        host.defer([this_host = &host, peer] { this_host->session_->on_peer_resumed(peer); });
    }

    SimulatedHost& host;
    session::PeerHandle handle;
    netsim::NodeAddress address;
    transport::SecureChannel channel;
    SimulatedDatagramSocket socket;
    transport::SecureControlPacketSink sink;
    transport::ReliableControlChannel control;
    transport::LivenessMonitor liveness;
    // admit() で決まる、ストリームごとの流し込み先と再生側。決まるまではメディアをミキサーへ
    // 流さない。信号状態は接続ごとの判定なので、先頭のストリームの再生側で判定する。
    std::vector<Route> routes;
    // 最後に受け取った送信ミュートの通知。あとから足したストリームの再生側にも渡す。
    std::optional<std::pair<bool, std::uint32_t>> send_mute;
    // 再送された同じ 1 つ目のハンドシェイクには、同じ応答を返し直す（接続を作り直さない）。
    std::vector<std::uint8_t> initiation;
    std::vector<std::uint8_t> response;
    std::vector<std::uint8_t> plaintext;
    std::optional<transport::SignalState> last_signal;
    HostPeerStats stats;
    // 同じ送信元から新しいハンドシェイクが来た。受信はもう振り分けない。
    bool superseded = false;
    bool closed = false;
};

SimulatedHost::SimulatedHost(const common::Clock& clock, netsim::SimulatedNetwork& network,
                             const netsim::NodeAddress& address,
                             const transport::PlayoutConfig& playout)
    : clock_(clock),
      network_(network),
      address_(address),
      playout_(playout),
      keys_(generate_keys()) {
    session::HostSession::Config config;
    std::copy(keys_.public_key().begin(), keys_.public_key().end(), config.host_public_key.begin());
    config.invite_base_url = "https://vocroz.test";
    // 出力デバイスは常に開いているものとする（デバイスの抜去は session の単体テストで確かめる）。
    // 保存した出力デバイスとして渡し、start() で開かせる。
    outputs_.add("harness-output", "Harness output");
    settings_.host() = session::HostSettings{.output_device_id = "harness-output"};
    session_ = std::make_unique<session::HostSession>(
        std::move(config),
        session::HostSession::Ports{ui_, mixer_, outputs_, *this, rendezvous_, settings_, random_,
                                    clock_, logger_, playout_control_});
    session_->start();
    network_.attach(
        address_, [this](const netsim::NodeAddress& from, std::span<const std::uint8_t> datagram) {
            on_datagram(from, datagram);
        });
    next_tick_ = clock_.now() + kSessionTickInterval;
}

SimulatedHost::~SimulatedHost() {
    network_.detach(address_);
}

const transport::PublicKey& SimulatedHost::public_key() const noexcept {
    return keys_.public_key();
}

void SimulatedHost::step() {
    const auto now = clock_.now();
    for (auto& [handle, peer] : peers_) {
        if (peer->closed) {
            continue;
        }
        peer->control.tick();
        peer->liveness.poll();
        for (Route& route : peer->routes) {
            route.receiver->poll(now);
        }
        if (peer->routes.empty()) {
            continue;
        }
        const auto signal = peer->routes.front().receiver->signal_state();
        if (signal != peer->last_signal) {
            peer->last_signal = signal;
            const auto id = handle;
            defer([this, id, signal] { session_->on_signal_state(id, to_protocol(signal)); });
        }
    }
    // 通知の配送の中で新しい通知が溜まることがあるので、空になるまで繰り返す。
    while (!deferred_.empty()) {
        auto actions = std::move(deferred_);
        deferred_.clear();
        for (auto& action : actions) {
            action();
        }
    }
    if (now >= next_tick_) {
        session_->tick();
        next_tick_ = now + kSessionTickInterval;
    }
    remove_closed();
}

void SimulatedHost::render(std::span<float> out) {
    std::fill(out.begin(), out.end(), 0.0F);
    scratch_.resize(out.size());
    for (auto& [handle, peer] : peers_) {
        if (peer->closed) {
            continue;
        }
        // 承認前の接続には再生側がなく、メディアも受け付けていないので、足すものがない。
        for (Route& route : peer->routes) {
            StripOutput& strip = strip_outputs_[route.strip_id];
            strip.left.resize(out.size());
            strip.right.resize(out.size());
            if (route.receiver->channels() == 1) {
                route.receiver->render(strip.left);
                std::copy(strip.left.begin(), strip.left.end(), strip.right.begin());
                std::copy(strip.left.begin(), strip.left.end(), scratch_.begin());
            } else {
                // 試験基盤のミキサーはモノラルなので、左右の平均を足す。
                route.receiver->render(strip.left, strip.right);
                for (std::size_t i = 0; i < out.size(); ++i) {
                    scratch_[i] = 0.5F * (strip.left[i] + strip.right[i]);
                }
            }
            mixer_.accumulate(route.strip_id, scratch_, out);
        }
    }
    mixer_.apply_master(out);
}

std::size_t SimulatedHost::peer_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        peers_.begin(), peers_.end(), [](const auto& entry) { return !entry.second->closed; }));
}

HostPeerStats SimulatedHost::total_stats() const noexcept {
    HostPeerStats total = closed_stats_;
    for (const auto& [handle, peer] : peers_) {
        total.media_before_admission += peer->stats.media_before_admission;
        total.media_admitted += peer->stats.media_admitted;
        total.media_unrouted += peer->stats.media_unrouted;
    }
    return total;
}

void SimulatedHost::send(session::PeerHandle peer, std::span<const std::uint8_t> message) {
    if (Peer* const found = find(peer)) {
        // 送信待ちが満杯なら失われるが、信頼制御層の上限は session のメッセージの量より十分大きい。
        static_cast<void>(found->control.send(transport::ControlLane::kControl, message));
    }
}

void SimulatedHost::admit(session::PeerHandle peer,
                          const std::vector<session::StreamRoute>& routes) {
    Peer* const found = find(peer);
    if (found == nullptr || routes.empty()) {
        return;
    }
    const bool first_admission = found->routes.empty();
    std::vector<Route> next;
    for (const auto& wanted : routes) {
        Route* existing = found->route(wanted.stream_id);
        if (existing != nullptr && existing->channels == wanted.channels) {
            // 同じストリームは再生側（ジッターバッファ）を作り直さない。
            existing->strip_id = wanted.strip_id;
            next.push_back(std::move(*existing));
            continue;
        }
        transport::PlayoutConfig config = playout_;
        config.channels = wanted.channels;
        auto created = MediaReceiver::create(found->channel.opener, config, clock_.now());
        if (!created) {
            std::abort();
        }
        Route route{wanted.stream_id, wanted.channels, wanted.strip_id, std::move(created).value()};
        if (found->send_mute) {
            route.receiver->on_send_mute(found->send_mute->first, found->send_mute->second,
                                         clock_.now());
        }
        next.push_back(std::move(route));
    }
    found->routes = std::move(next);
    if (first_admission) {
        // 製品では再生側の準備（ミキサーへの組み込み）が済んでから知らせる。ここでは次の step()
        // で知らせる。
        defer([this, peer] { session_->on_peer_established(peer); });
    }
}

void SimulatedHost::apply_send_mute(session::PeerHandle peer, bool send_muted,
                                    std::uint32_t epoch) {
    if (Peer* const found = find(peer)) {
        found->send_mute = std::pair{send_muted, epoch};
        for (Route& route : found->routes) {
            route.receiver->on_send_mute(send_muted, epoch, clock_.now());
        }
    }
}

void SimulatedHost::set_return_mute(session::PeerHandle /*peer*/, bool /*muted*/,
                                    std::uint32_t /*epoch*/) {
    // この試験基盤の親機は返送（下りの音声）を送らない。返送のミックスとエンコードは
    // audio-core と transport の単体テストで確かめる（docs/adr/0036-return-monitor.md）。
}

void SimulatedHost::close(session::PeerHandle peer) {
    // 呼び出し元が、まさにこの接続の受信の処理の中にいることがあるので、破棄は step()
    // の最後に行う。
    if (Peer* const found = find(peer)) {
        found->closed = true;
    }
}

void SimulatedHost::on_datagram(const netsim::NodeAddress& from,
                                std::span<const std::uint8_t> datagram) {
    const auto header = transport::parse_packet_header(datagram);
    if (!header) {
        return;
    }
    if (header.value().type == transport::PacketType::kHandshakeInitiation) {
        accept_handshake(from, datagram);
        return;
    }
    Peer* const peer = find(from);
    if (peer == nullptr) {
        return;
    }
    const auto opened = peer->channel.opener.open(datagram, peer->plaintext);
    if (!opened) {
        return;
    }
    peer->liveness.note_packet_received();
    if (opened.value().type == transport::PacketType::kControl) {
        static_cast<void>(peer->control.on_control_packet(opened.value().plaintext));
        return;
    }
    if (opened.value().type != transport::PacketType::kMedia) {
        return;
    }
    const auto media = transport::decode_media_payload(opened.value().plaintext);
    if (!media) {
        return;
    }
    if (peer->routes.empty()) {
        // 承認前の接続のメディアは受け付けない（承認前に音が届かない）。
        ++peer->stats.media_before_admission;
        return;
    }
    Route* const route = peer->route(media.value().stream_id);
    if (route == nullptr) {
        // 申告のないストリームは、流し込むストリップがないので捨てる。
        ++peer->stats.media_unrouted;
        return;
    }
    ++peer->stats.media_admitted;
    route->receiver->on_media(media.value(), clock_.now());
}

void SimulatedHost::accept_handshake(const netsim::NodeAddress& from,
                                     std::span<const std::uint8_t> datagram) {
    Peer* const existing = find(from);
    if (existing != nullptr &&
        std::equal(datagram.begin(), datagram.end(), existing->initiation.begin(),
                   existing->initiation.end())) {
        // 応答が失われて子機が再送した。同じ応答を返し直す。
        network_.send(address_, from, existing->response);
        return;
    }
    auto responder = transport::NoiseIkHandshake::create_responder(keys_);
    if (!responder) {
        return;
    }
    std::vector<std::uint8_t> payload(datagram.size());
    if (!responder.value().read_initiation(datagram, payload)) {
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
    if (existing != nullptr) {
        // 同じ送信元からの新しいハンドシェイクは、子機が接続を張り直したもの。以後の受信は新しい接続へ回す。
        existing->superseded = true;
        if (existing->routes.empty()) {
            // 承認前の接続は戻る先がないので、申請ごと閉じる。
            existing->closed = true;
            const auto old = existing->handle;
            defer([this, old] { session_->on_peer_closed(old); });
        }
        // 承認済みの接続は、再接続の申請（ReconnectRequest）で session が close() するまで残す。
        // ここで閉じたと知らせると、session は子機が退出したとみなしてストリップを片付けてしまう。
    }
    const auto handle = next_handle_++;
    peers_.emplace(handle, std::make_unique<Peer>(*this, handle, from, std::move(secure).value(),
                                                  datagram, response));
    network_.send(address_, from, response);
}

SimulatedHost::Peer* SimulatedHost::find(session::PeerHandle handle) {
    const auto found = peers_.find(handle);
    return found == peers_.end() || found->second->closed ? nullptr : found->second.get();
}

SimulatedHost::Peer* SimulatedHost::find(const netsim::NodeAddress& address) {
    for (auto& [handle, peer] : peers_) {
        if (!peer->closed && !peer->superseded && peer->address == address) {
            return peer.get();
        }
    }
    return nullptr;
}

void SimulatedHost::defer(std::function<void()> action) {
    deferred_.push_back(std::move(action));
}

void SimulatedHost::remove_closed() {
    for (auto it = peers_.begin(); it != peers_.end();) {
        if (it->second->closed) {
            closed_stats_.media_before_admission += it->second->stats.media_before_admission;
            closed_stats_.media_admitted += it->second->stats.media_admitted;
            closed_stats_.media_unrouted += it->second->stats.media_unrouted;
            it = peers_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace vocroz::test_harness
