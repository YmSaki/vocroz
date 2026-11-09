// SPDX-License-Identifier: AGPL-3.0-only
//
// PeerPacketSender の単体テスト。制御パケットとメディアフレームが送信待ちに入り、pump() で
// 暗号化されて送られ、相手の PacketOpener で元に戻ること、制御を先に送ること、満杯と失敗の
// 扱いを確かめる。

#include "vocroz/transport/peer_packet_sender.h"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/transport/media_payload.h"
#include "vocroz/transport/noise_ik_handshake.h"
#include "vocroz/transport/packet_header.h"

#include "fakes/fake_datagram_socket.h"

namespace vocroz::transport {
namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint16_t kStreamId = 7;

// 通知の回数を数える。
class CountingNotifier final : public SendNotifier {
public:
    void notify() noexcept VOCROZ_NONBLOCKING override {
        ++count;
    }
    int count = 0;
};

// Noise_IK のハンドシェイクを最後まで行い、送る側と受ける側の SecureChannel を返す。
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

MediaFrame frame_at(std::uint64_t timestamp, std::uint32_t epoch) {
    const Bytes payload{0x11, 0x22, 0x33};
    return make_media_frame(timestamp, epoch, payload).value();
}

class PeerPacketSenderTest : public ::testing::Test {
protected:
    PeerPacketSenderTest() : channels_(establish()) {}

    // 送ったデータグラムを開く。
    OpenedPacket open(std::size_t index) {
        auto opened = channels_.second.opener.open(socket_.datagrams.at(index), buffer_);
        EXPECT_TRUE(opened);
        return opened.value();
    }

    std::pair<SecureChannel, SecureChannel> channels_;
    FakeDatagramSocket socket_;
    CountingNotifier notifier_;
    PeerPacketSender sender_{std::move(channels_.first.sealer), socket_, kStreamId, notifier_};
    Bytes buffer_ = Bytes(kMaxPacketBytes);
};

TEST_F(PeerPacketSenderTest, QueuesUntilPumpedAndNotifiesEachTime) {
    const Bytes plaintext{0x02, 0x42};
    ASSERT_TRUE(sender_.send_control_packet(plaintext));
    ASSERT_TRUE(sender_.push_frame(frame_at(480, 3)));
    // 送信スレッドが pump() するまでは送らない（呼び出し側のスレッドでソケットに触れない）。
    EXPECT_TRUE(socket_.datagrams.empty());
    EXPECT_EQ(notifier_.count, 2);

    sender_.pump();
    ASSERT_EQ(socket_.datagrams.size(), 2U);
    const auto control = open(0);
    EXPECT_EQ(control.type, PacketType::kControl);
    EXPECT_EQ(Bytes(control.plaintext.begin(), control.plaintext.end()), plaintext);
    const auto media = open(1);
    ASSERT_EQ(media.type, PacketType::kMedia);
    const auto payload = decode_media_payload(media.plaintext);
    ASSERT_TRUE(payload);
    EXPECT_EQ(payload.value().stream_id, kStreamId);
    EXPECT_EQ(payload.value().timestamp, 480U);
    EXPECT_EQ(payload.value().mute_epoch, 3U);
    EXPECT_EQ(sender_.stats().control_sent, 1U);
    EXPECT_EQ(sender_.stats().media_sent, 1U);
}

TEST_F(PeerPacketSenderTest, SendsControlBeforeMediaQueuedEarlier) {
    ASSERT_TRUE(sender_.push_frame(frame_at(0, 1)));
    ASSERT_TRUE(sender_.send_control_packet({}));
    sender_.pump();
    ASSERT_EQ(socket_.datagrams.size(), 2U);
    EXPECT_EQ(open(0).type, PacketType::kControl);
    EXPECT_EQ(open(1).type, PacketType::kMedia);
}

TEST_F(PeerPacketSenderTest, DropsFramesWhenTheQueueIsFull) {
    for (std::size_t i = 0; i < PeerPacketSender::kFrameQueueCapacity; ++i) {
        ASSERT_TRUE(sender_.push_frame(frame_at(i * 240, 1)));
    }
    EXPECT_FALSE(sender_.push_frame(frame_at(0, 1)));
    EXPECT_EQ(sender_.stats().dropped, 1U);
}

TEST_F(PeerPacketSenderTest, RejectsControlThatDoesNotFitOrIsFull) {
    const Bytes too_long(kMaxPlaintextBytes + 1);
    ASSERT_FALSE(sender_.send_control_packet(too_long));
    EXPECT_EQ(sender_.send_control_packet(too_long).error(), TransportError::kPayloadTooLarge);
    for (std::size_t i = 0; i < PeerPacketSender::kControlQueueCapacity; ++i) {
        ASSERT_TRUE(sender_.send_control_packet({}));
    }
    const auto full = sender_.send_control_packet({});
    ASSERT_FALSE(full);
    EXPECT_EQ(full.error(), TransportError::kSendFailed);
}

TEST_F(PeerPacketSenderTest, CountsSocketFailures) {
    socket_.fail_sends = true;
    ASSERT_TRUE(sender_.push_frame(frame_at(0, 1)));
    sender_.pump();
    EXPECT_EQ(sender_.stats().send_failures, 1U);
    EXPECT_EQ(sender_.stats().media_sent, 0U);
}

}  // namespace
}  // namespace vocroz::transport
