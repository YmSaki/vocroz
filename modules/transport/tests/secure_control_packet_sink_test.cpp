// SPDX-License-Identifier: AGPL-3.0-only
//
// SecureControlPacketSink の単体テスト。信頼制御層の平文が暗号化されてソケットに渡り、
// 相手の PacketOpener で同じ平文に戻ること、ソケットの失敗がエラーとして返ることを確かめる。

#include "vocroz/transport/secure_control_packet_sink.h"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/transport/noise_ik_handshake.h"
#include "vocroz/transport/packet_header.h"

#include "fakes/fake_datagram_socket.h"

namespace vocroz::transport {
namespace {

using Bytes = std::vector<std::uint8_t>;

// Noise_IK のハンドシェイクを最後まで行い、子機側と親機側の SecureChannel を返す。
std::pair<SecureChannel, SecureChannel> establish() {
    auto client_keys = StaticKeyPair::generate();
    auto host_keys = StaticKeyPair::generate();
    EXPECT_TRUE(client_keys && host_keys);
    auto initiator = NoiseIkHandshake::create_initiator(client_keys.value(),
                                                        host_keys.value().public_key(), 0x01020304);
    auto responder = NoiseIkHandshake::create_responder(host_keys.value());
    EXPECT_TRUE(initiator && responder);
    Bytes packet(kMaxPacketBytes);
    Bytes payload(kMaxPacketBytes);
    const auto initiation = initiator.value().write_initiation({}, packet);
    EXPECT_TRUE(
        responder.value().read_initiation(std::span(packet).first(initiation.value()), payload));
    const auto response = responder.value().write_response({}, packet);
    EXPECT_TRUE(
        initiator.value().read_response(std::span(packet).first(response.value()), payload));
    auto client = initiator.value().finish();
    auto host = responder.value().finish();
    EXPECT_TRUE(client && host);
    return {std::move(client).value(), std::move(host).value()};
}

TEST(SecureControlPacketSinkTest, SealsPlaintextIntoControlPacketThePeerCanOpen) {
    auto [client, host] = establish();
    FakeDatagramSocket socket;
    SecureControlPacketSink sink(client.sealer, socket);

    const Bytes plaintext{0x02, 0x00, 0, 0, 0, 0, 0, 1, 0x42};
    ASSERT_TRUE(sink.send_control_packet(plaintext));
    ASSERT_TRUE(sink.send_control_packet({}));  // キープアライブ
    ASSERT_EQ(socket.datagrams.size(), 2U);

    Bytes opened(kMaxPacketBytes);
    const auto first = host.opener.open(socket.datagrams[0], opened);
    ASSERT_TRUE(first);
    EXPECT_EQ(first.value().type, PacketType::kControl);
    EXPECT_EQ(Bytes(first.value().plaintext.begin(), first.value().plaintext.end()), plaintext);
    const auto keepalive = host.opener.open(socket.datagrams[1], opened);
    ASSERT_TRUE(keepalive);
    EXPECT_TRUE(keepalive.value().plaintext.empty());
}

TEST(SecureControlPacketSinkTest, ReturnsSocketFailure) {
    auto [client, host] = establish();
    FakeDatagramSocket socket;
    socket.fail_sends = true;
    SecureControlPacketSink sink(client.sealer, socket);

    const auto result = sink.send_control_packet({});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), TransportError::kSendFailed);
}

}  // namespace
}  // namespace vocroz::transport
