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
          plaintext(transport::kMaxPacketBytes) {
        auto created = MediaReceiver::create(channel.opener, owner.playout_, owner.clock_.now());
        if (!created) {
            std::abort();
        }
        receiver = std::move(created).value();
        last_signal = receiver->signal_state();
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
    std::unique_ptr<MediaReceiver> receiver;
    // 再送された同じ 1 つ目のハンドシェイクには、同じ応答を返し直す（接続を作り直さない）。
    std::vector<std::uint8_t> initiation;
    std::vector<std::uint8_t> response;
    std::vector<std::uint8_t> plaintext;
    // admit() で決まるストリップ。決まるまではメディアをミキサーへ流さない。
    std::optional<std::string> strip;
    transport::SignalState last_signal;
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
        peer->receiver->poll(now);
        const auto signal = peer->receiver->signal_state();
        if (signal != peer->last_signal && peer->strip) {
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
        // 承認前の接続も再生側は動かし続ける（溜まったフレームを古いまま残さないため）が、
        // ストリップがないのでミキサーには足さない。
        peer->receiver->render(scratch_);
        if (peer->strip) {
            mixer_.accumulate(*peer->strip, scratch_, out);
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
    // この試験基盤の子機はモノラルのマイク 1 本（ストリーム 0）だけを送るので、1
    // 本目の対応だけを使う。
    const bool first_admission = !found->strip;
    found->strip = routes.front().strip_id;
    if (first_admission) {
        // 製品では再生側の準備（ミキサーへの組み込み）が済んでから知らせる。ここでは次の step()
        // で知らせる。
        defer([this, peer] { session_->on_peer_established(peer); });
    }
}

void SimulatedHost::apply_send_mute(session::PeerHandle peer, bool send_muted,
                                    std::uint32_t epoch) {
    if (Peer* const found = find(peer)) {
        found->receiver->on_send_mute(send_muted, epoch, clock_.now());
    }
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
    if (!peer->strip) {
        // 承認前の接続のメディアは受け付けない（承認前に音が届かない）。
        ++peer->stats.media_before_admission;
        return;
    }
    ++peer->stats.media_admitted;
    peer->receiver->on_media(media.value(), clock_.now());
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
        if (!existing->strip) {
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
            it = peers_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace vocroz::test_harness
