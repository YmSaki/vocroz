// SPDX-License-Identifier: AGPL-3.0-only
//
// SimulatedClient の実装。

#include "vocroz/test_harness/simulated_client.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <utility>

#include "vocroz/test_harness/media_sender.h"
#include "vocroz/test_harness/simulated_datagram_socket.h"
#include "vocroz/transport/constants.h"
#include "vocroz/transport/control_lane.h"
#include "vocroz/transport/liveness_monitor.h"
#include "vocroz/transport/packet_header.h"
#include "vocroz/transport/reliable_control_channel.h"
#include "vocroz/transport/secure_control_packet_sink.h"

namespace vocroz::test_harness {
namespace {

// session の tick() の間隔。session の README の推奨（100 ms〜1 s）の短い側にして、
// 再接続の猶予の判定の遅れを小さくする。
constexpr std::chrono::milliseconds kSessionTickInterval{100};

// 応答が届かないときに、ハンドシェイクの 1 つ目を送り直す間隔。
// 経路が途絶えている間も送り続け、戻ったらすぐに張り直せるようにする。
constexpr std::chrono::milliseconds kHandshakeRetryInterval{200};

transport::StaticKeyPair generate_keys() {
    auto keys = transport::StaticKeyPair::generate();
    if (!keys) {
        // 鍵を作れない環境では結合テストを続けられない。
        std::abort();
    }
    return std::move(keys).value();
}

}  // namespace

// 確立した 1 つの暗号化セッション。張り直すたびに作り直す。
struct SimulatedClient::Link {
    Link(SimulatedClient& owner, transport::SecureChannel secure)
        : channel(std::move(secure)),
          socket(owner.network_, owner.address_, owner.host_),
          sink(channel.sealer, socket),
          control(owner.clock_, sink, owner),
          liveness(owner.clock_, owner),
          plaintext(transport::kMaxPacketBytes) {
        configure(owner);
    }

    // 入力の構成に送信側をそろえる。変わらない入力の送信側（タイムスタンプ）はそのまま残す。
    void configure(const SimulatedClient& owner) {
        std::vector<Stream> next;
        for (const auto& stream : owner.streams_) {
            const auto it =
                std::ranges::find_if(streams, [&](const Stream& s) { return s.stream == stream; });
            if (it != streams.end()) {
                next.push_back(std::move(*it));
                continue;
            }
            transport::OpusEncoderConfig config = owner.config_.encoder;
            config.channels = stream.channels;
            auto created = MediaSender::create(channel.sealer, socket, config, stream.stream_id,
                                               owner.mute_epoch_);
            if (!created) {
                std::abort();
            }
            next.push_back(
                Stream{stream, std::make_unique<MediaSender>(std::move(created).value())});
        }
        streams = std::move(next);
    }

    // 入力の種類の送信側。その入力を送らない構成なら nullptr。
    MediaSender* sender(control_protocol::InputSourceKind kind) {
        const auto it =
            std::ranges::find_if(streams, [&](const Stream& s) { return s.stream.kind == kind; });
        return it == streams.end() ? nullptr : it->sender.get();
    }

    struct Stream {
        session::InputStream stream;
        std::unique_ptr<MediaSender> sender;
    };

    transport::SecureChannel channel;
    SimulatedDatagramSocket socket;
    transport::SecureControlPacketSink sink;
    transport::ReliableControlChannel control;
    transport::LivenessMonitor liveness;
    std::vector<Stream> streams;
    std::vector<std::uint8_t> plaintext;
};

SimulatedClient::SimulatedClient(const common::Clock& clock, netsim::SimulatedNetwork& network,
                                 const netsim::NodeAddress& address,
                                 const netsim::NodeAddress& host, SimulatedClientConfig config)
    : clock_(clock),
      network_(network),
      address_(address),
      host_(host),
      config_(std::move(config)),
      keys_(generate_keys()) {
    settings_.client() = session::ClientSettings{config_.display_name, "harness-mic", 0.0};
    session_ = std::make_unique<session::ClientSession>(
        session::ClientSession::Ports{ui_, *this, *this, rendezvous_, settings_, clock_, logger_});
    session_->start();
    session_->on_input_devices_changed(
        {control_protocol::InputDevice{.id = "harness-mic", .name = "Harness mic"}});
    network_.attach(
        address_, [this](const netsim::NodeAddress& from, std::span<const std::uint8_t> datagram) {
            on_datagram(from, datagram);
        });
    next_tick_ = clock_.now() + kSessionTickInterval;
}

SimulatedClient::~SimulatedClient() {
    network_.detach(address_);
}

void SimulatedClient::capture(std::span<const float> block) {
    capture_input(control_protocol::InputSourceKind::kMicrophone, block);
}

void SimulatedClient::capture_app_audio(std::span<const float> block) {
    capture_input(control_protocol::InputSourceKind::kApplicationAudio, block);
}

void SimulatedClient::capture_input(control_protocol::InputSourceKind kind,
                                    std::span<const float> block) {
    if (!link_ || (send_muted_ && !config_.ignore_send_mute)) {
        // 送信ミュート中は送らない（親機ではミュートの通知で PAUSED として扱われる）。
        return;
    }
    MediaSender* const sender = link_->sender(kind);
    if (sender == nullptr) {
        return;  // その入力を送らない構成
    }
    const std::uint64_t before = sender->stats().frames_sent;
    sender->capture(block);
    stats_.media_frames_sent += sender->stats().frames_sent - before;
}

void SimulatedClient::step() {
    const auto now = clock_.now();
    if (handshake_ && now >= next_initiation_) {
        network_.send(address_, host_, initiation_);
        next_initiation_ = now + kHandshakeRetryInterval;
    }
    if (link_) {
        link_->control.tick();
        link_->liveness.poll();
    }
    // 通知の配送の中で新しい通知が溜まることがあるので、空になるまで繰り返す。
    while (!deferred_.empty()) {
        auto actions = std::move(deferred_);
        deferred_.clear();
        for (auto& action : actions) {
            action();
        }
    }
    apply_link_request();
    if (link_ && session_->snapshot().client_state == control_protocol::ClientState::kConnecting) {
        // 製品では送信の準備（キャプチャとエンコーダーの起動）が済んだら知らせる。
        // ここでは承認を受けた次の step() で知らせる。
        session_->on_media_established();
    }
    if (now >= next_tick_) {
        session_->tick();
        next_tick_ = now + kSessionTickInterval;
    }
}

void SimulatedClient::select_input(const std::optional<std::string>& /*device_id*/) {
    // 入力は常にテストが capture() で渡すので、デバイスの選択は記録しない。
}

void SimulatedClient::set_send_muted(bool muted, std::uint32_t epoch) {
    send_muted_ = muted;
    mute_epoch_ = epoch;
    if (link_) {
        for (auto& stream : link_->streams) {
            stream.sender->set_mute_epoch(epoch);
        }
    }
}

void SimulatedClient::set_send_level(double /*level_db*/) {
    // 送信レベルは音の大きさだけの話で、結合テストの判定に使わない。
}

void SimulatedClient::set_streams(const std::vector<session::InputStream>& streams) {
    streams_ = streams;
    if (link_) {
        link_->configure(*this);
    }
}

void SimulatedClient::connect(const session::HostEndpoint& endpoint) {
    transport::PublicKey key{};
    std::copy(endpoint.host_public_key.begin(), endpoint.host_public_key.end(), key.begin());
    host_public_key_ = key;
    link_request_ = LinkRequest::kConnect;
}

void SimulatedClient::reconnect() {
    link_request_ = LinkRequest::kConnect;
}

void SimulatedClient::send(std::span<const std::uint8_t> message) {
    if (link_) {
        // 送信待ちが満杯なら失われるが、信頼制御層の上限は session のメッセージの量より十分大きい。
        static_cast<void>(link_->control.send(transport::ControlLane::kControl, message));
    }
}

void SimulatedClient::apply_return_mute(bool /*muted*/, std::uint32_t /*epoch*/) {
    // この試験基盤の子機は返送（下りの音声）を受信しない（SimulatedHost::set_return_mute()）。
}

void SimulatedClient::close() {
    link_request_ = LinkRequest::kClose;
}

void SimulatedClient::on_control_message(transport::ControlLane lane,
                                         std::span<const std::uint8_t> message) {
    if (lane == transport::ControlLane::kControl) {
        session_->on_link_message(message);
    }
}

void SimulatedClient::on_peer_timed_out() {
    defer([this] { session_->on_link_lost(); });
}

void SimulatedClient::on_peer_resumed() {
    defer([this] { session_->on_link_resumed(); });
}

void SimulatedClient::on_datagram(const netsim::NodeAddress& from,
                                  std::span<const std::uint8_t> datagram) {
    if (from != host_) {
        return;
    }
    const auto header = transport::parse_packet_header(datagram);
    if (!header) {
        return;
    }
    if (header.value().type == transport::PacketType::kHandshakeResponse) {
        on_handshake_response(datagram);
        return;
    }
    if (!link_) {
        return;
    }
    const auto opened = link_->channel.opener.open(datagram, link_->plaintext);
    if (!opened) {
        return;
    }
    link_->liveness.note_packet_received();
    if (opened.value().type == transport::PacketType::kControl) {
        static_cast<void>(link_->control.on_control_packet(opened.value().plaintext));
    }
}

void SimulatedClient::on_handshake_response(std::span<const std::uint8_t> datagram) {
    if (!handshake_) {
        return;  // 再送への応答が重なって届いた
    }
    std::vector<std::uint8_t> payload(datagram.size());
    if (!handshake_->read_response(datagram, payload)) {
        return;
    }
    auto secure = handshake_->finish();
    handshake_.reset();
    if (!secure) {
        return;
    }
    link_ = std::make_unique<Link>(*this, std::move(secure).value());
    ++stats_.links_established;
    defer([this] { session_->on_link_established(); });
}

void SimulatedClient::start_handshake() {
    const auto connection_id = transport::generate_connection_id();
    if (!connection_id || !host_public_key_) {
        return;
    }
    auto initiator = transport::NoiseIkHandshake::create_initiator(keys_, *host_public_key_,
                                                                   connection_id.value());
    if (!initiator) {
        return;
    }
    initiation_.assign(transport::kMaxPacketBytes, 0);
    const auto written = initiator.value().write_initiation({}, initiation_);
    if (!written) {
        return;
    }
    initiation_.resize(written.value());
    handshake_ = std::move(initiator).value();
    // 次の step() ですぐに送る。
    next_initiation_ = clock_.now();
}

void SimulatedClient::apply_link_request() {
    const auto request = std::exchange(link_request_, LinkRequest::kNone);
    switch (request) {
        case LinkRequest::kNone:
            return;
        case LinkRequest::kConnect:
            link_.reset();
            start_handshake();
            return;
        case LinkRequest::kClose:
            link_.reset();
            handshake_.reset();
            return;
    }
}

void SimulatedClient::defer(std::function<void()> action) {
    deferred_.push_back(std::move(action));
}

}  // namespace vocroz::test_harness
