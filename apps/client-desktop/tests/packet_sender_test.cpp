// SPDX-License-Identifier: AGPL-3.0-only
//
// 送信スレッドの暗号化と送信（PacketSender）のテスト。メディアと制御を同じ送信方向の鍵で
// 暗号化すること、送信先がない間は捨てること、古い接続の制御パケットを新しい接続で送らないことを
// 確かめる。

#include "packet_sender.h"

#include <algorithm>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/transport/media_payload.h"

#include "fakes/fake_datagram_socket.h"
#include "secure_pair.h"

namespace vocroz::client {
namespace {

// 送信スレッドの後で中身を確かめられるよう、送られたデータグラムを外に残すソケット。
class SharedSocket final : public transport::DatagramSocket {
public:
    explicit SharedSocket(transport::FakeDatagramSocket& target) : target_(target) {}
    [[nodiscard]] common::Result<void, transport::TransportError> send(
        std::span<const std::uint8_t> datagram) noexcept override {
        return target_.send(datagram);
    }

private:
    transport::FakeDatagramSocket& target_;
};

class PacketSenderTest : public ::testing::Test {
protected:
    void install_path() {
        auto pair = test::make_secure_pair();
        host_ = std::make_unique<transport::SecureChannel>(std::move(pair.host));
        sender_.set_path(std::make_unique<SendPath>(
            SendPath{std::move(pair.client.sealer), std::make_unique<SharedSocket>(socket_), 0}));
    }

    transport::OpenedPacket open(std::size_t index) {
        plaintext_.assign(transport::kMaxPacketBytes, 0);
        return host_->opener.open(socket_.datagrams.at(index), plaintext_).value();
    }

    static EncodedFrame frame(std::uint64_t timestamp, std::uint32_t epoch) {
        EncodedFrame f;
        f.timestamp = timestamp;
        f.mute_epoch = epoch;
        f.size = 3;
        f.bytes[0] = 0xAA;
        return f;
    }

    transport::SendWakeup wakeup_;
    PacketSender sender_{wakeup_};
    transport::FakeDatagramSocket socket_;
    std::unique_ptr<transport::SecureChannel> host_;
    std::vector<std::uint8_t> plaintext_;
};

TEST_F(PacketSenderTest, SendsFramesOfEveryLaneWithTheirStreamIds) {
    // マイクとアプリ音声は別のオーディオスレッドから、別の経路で入る。
    install_path();
    auto app = frame(960, 1);
    app.stream_id = 1;
    ASSERT_TRUE(sender_.push_frame(kAppAudioLane, app));
    ASSERT_TRUE(sender_.push_frame(kMicrophoneLane, frame(480, 1)));
    EXPECT_FALSE(sender_.push_frame(kCaptureLaneCount, frame(0, 0)));
    sender_.pump();

    ASSERT_EQ(socket_.datagrams.size(), 2U);
    std::vector<std::uint16_t> ids;
    for (std::size_t i = 0; i < 2; ++i) {
        ids.push_back(transport::decode_media_payload(open(i).plaintext).value().stream_id);
    }
    std::ranges::sort(ids);
    EXPECT_EQ(ids, (std::vector<std::uint16_t>{0, 1}));
}

TEST_F(PacketSenderTest, SealsFramesAsMediaPackets) {
    install_path();
    ASSERT_TRUE(sender_.push_frame(kMicrophoneLane, frame(480, 2)));
    sender_.pump();

    ASSERT_EQ(socket_.datagrams.size(), 1U);
    const auto opened = open(0);
    EXPECT_EQ(opened.type, transport::PacketType::kMedia);
    const auto media = transport::decode_media_payload(opened.plaintext).value();
    EXPECT_EQ(media.stream_id, 0U);
    EXPECT_EQ(media.timestamp, 480U);
    EXPECT_EQ(media.mute_epoch, 2U);
    EXPECT_EQ(media.opus_payload.size(), 3U);
    EXPECT_EQ(sender_.stats().media_sent, 1U);
}

TEST_F(PacketSenderTest, SealsControlBeforeMediaWithOneSequence) {
    install_path();
    ASSERT_TRUE(sender_.push_frame(kMicrophoneLane, frame(0, 0)));
    const std::vector<std::uint8_t> control = {1, 2, 3};
    ASSERT_TRUE(sender_.push_control(control));
    sender_.pump();

    ASSERT_EQ(socket_.datagrams.size(), 2U);
    // 同じ鍵とシーケンス番号の列で暗号化しているので、受信側はどちらも復号できる。
    const auto first = open(0);
    EXPECT_EQ(first.type, transport::PacketType::kControl);
    EXPECT_EQ(std::vector(first.plaintext.begin(), first.plaintext.end()), control);
    EXPECT_EQ(open(1).type, transport::PacketType::kMedia);
}

TEST_F(PacketSenderTest, DropsPacketsWithoutPath) {
    ASSERT_TRUE(sender_.push_frame(kMicrophoneLane, frame(0, 0)));
    ASSERT_TRUE(sender_.push_control(std::vector<std::uint8_t>{1}));
    sender_.pump();

    EXPECT_TRUE(socket_.datagrams.empty());
    EXPECT_EQ(sender_.stats().dropped_no_path, 2U);
}

TEST_F(PacketSenderTest, DoesNotSendControlQueuedForPreviousPath) {
    install_path();
    ASSERT_TRUE(sender_.push_control(std::vector<std::uint8_t>{1}));
    // 送信スレッドが取り出す前に接続を張り直した。古い信頼制御層の平文は新しい鍵で送らない。
    install_path();
    ASSERT_TRUE(sender_.push_control(std::vector<std::uint8_t>{2}));
    sender_.pump();

    ASSERT_EQ(socket_.datagrams.size(), 1U);
    const auto opened = open(0);
    EXPECT_EQ(opened.plaintext[0], 2);
}

TEST_F(PacketSenderTest, StopsSendingAfterPathIsCleared) {
    install_path();
    sender_.set_path(nullptr);
    ASSERT_TRUE(sender_.push_frame(kMicrophoneLane, frame(0, 0)));
    sender_.pump();

    EXPECT_TRUE(socket_.datagrams.empty());
}

TEST_F(PacketSenderTest, RejectsControlLongerThanPlaintextLimit) {
    const std::vector<std::uint8_t> too_long(transport::kMaxPlaintextBytes + 1);
    EXPECT_FALSE(sender_.push_control(too_long));
}

TEST_F(PacketSenderTest, CountsSendFailures) {
    install_path();
    socket_.fail_sends = true;
    ASSERT_TRUE(sender_.push_frame(kMicrophoneLane, frame(0, 0)));
    sender_.pump();

    EXPECT_EQ(sender_.stats().send_failures, 1U);
}

TEST_F(PacketSenderTest, PushingWakesTheSenderThread) {
    const auto before = wakeup_.generation();
    ASSERT_TRUE(sender_.push_frame(kMicrophoneLane, frame(0, 0)));
    EXPECT_NE(wakeup_.generation(), before);
}

}  // namespace
}  // namespace vocroz::client
