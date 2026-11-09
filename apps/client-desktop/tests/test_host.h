// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の親機側の最小限の組み立て。子機が送った Noise_IK の 1 つ目のメッセージに応答し、
// 制御パケットを信頼制御層で受け取り、親機からの制御メッセージを子機の受け箱へ送る。
// 製品の親機（pa-engine）の代わりに、子機の接続の手順を確かめるために使う。
#pragma once

#include <cstdlib>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "vocroz/common/ports/clock.h"
#include "vocroz/network/udp_address.h"
#include "vocroz/transport/control_lane.h"
#include "vocroz/transport/noise_ik_handshake.h"
#include "vocroz/transport/packet_header.h"
#include "vocroz/transport/ports/control_message_listener.h"
#include "vocroz/transport/ports/control_packet_sink.h"
#include "vocroz/transport/reliable_control_channel.h"

#include "peer_inbox.h"
#include "secure_pair.h"

namespace vocroz::client::test {

/// 親機側の最小限の組み立て。
class TestHost final : public transport::ControlMessageListener,
                       public transport::ControlPacketSink {
public:
    /// @param clock   信頼制御層の時刻
    /// @param address 親機のアドレス（子機の受け箱には、この送信元で届ける）
    /// @param inbox   子機の受け箱
    TestHost(const common::Clock& clock, const network::UdpAddress& address, PeerInbox& inbox)
        : clock_(clock), address_(address), inbox_(inbox), keys_(make_keys()) {}

    /// 親機の公開鍵。
    [[nodiscard]] const transport::PublicKey& public_key() const noexcept {
        return keys_.public_key();
    }

    /// 子機から届いたデータグラムを処理する。1 つ目のメッセージなら応答して鍵を確定し、
    /// 暗号化されたパケットなら復号して信頼制御層へ渡す。
    void receive(std::span<const std::uint8_t> datagram) {
        const auto header = transport::parse_packet_header(datagram);
        if (!header) {
            return;
        }
        if (header.value().type == transport::PacketType::kHandshakeInitiation) {
            respond(datagram);
            return;
        }
        if (!channel_) {
            return;
        }
        std::vector<std::uint8_t> plaintext(transport::kMaxPacketBytes);
        const auto opened = channel_->opener.open(datagram, plaintext);
        if (!opened) {
            return;
        }
        if (opened.value().type == transport::PacketType::kMedia) {
            ++media_received;
        } else if (opened.value().type == transport::PacketType::kControl) {
            static_cast<void>(control_->on_control_packet(opened.value().plaintext));
        }
    }

    /// 制御メッセージを子機へ送る。
    void send(std::span<const std::uint8_t> message) {
        static_cast<void>(control_->send(transport::ControlLane::kControl, message));
    }

    /// 親機が応答したハンドシェイクの数。
    int handshakes = 0;
    /// 受け取ったメディアパケットの数。
    int media_received = 0;
    /// 子機から受け取った制御メッセージ。
    std::vector<std::vector<std::uint8_t>> messages;

    void on_control_message(transport::ControlLane /*lane*/,
                            std::span<const std::uint8_t> message) override {
        messages.emplace_back(message.begin(), message.end());
    }

    [[nodiscard]] common::Result<void, transport::TransportError> send_control_packet(
        std::span<const std::uint8_t> plaintext) noexcept override {
        std::vector<std::uint8_t> packet(transport::kMaxPacketBytes);
        const auto sealed = channel_->sealer.seal_control(plaintext, packet);
        if (!sealed) {
            return common::make_unexpected(sealed.error());
        }
        inbox_.on_datagram(address_, std::span(packet).first(sealed.value()));
        return {};
    }

private:
    void respond(std::span<const std::uint8_t> initiation) {
        auto responder = transport::NoiseIkHandshake::create_responder(keys_).value();
        std::vector<std::uint8_t> payload(transport::kMaxPacketBytes);
        if (!responder.read_initiation(initiation, payload)) {
            return;
        }
        std::vector<std::uint8_t> response(transport::kMaxPacketBytes);
        const auto written = responder.write_response({}, response).value();
        control_.reset();
        channel_.reset();
        channel_.emplace(responder.finish().value());
        control_ = std::make_unique<transport::ReliableControlChannel>(clock_, *this, *this);
        ++handshakes;
        inbox_.on_datagram(address_, std::span(response).first(written));
    }

    const common::Clock& clock_;
    network::UdpAddress address_;
    PeerInbox& inbox_;
    transport::StaticKeyPair keys_;
    std::optional<transport::SecureChannel> channel_;
    std::unique_ptr<transport::ReliableControlChannel> control_;
};

}  // namespace vocroz::client::test
