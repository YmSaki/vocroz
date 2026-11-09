// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機の送信側のオーディオスレッドの処理（CaptureProcessor）のテスト。初期ミュートでは何も送らない
// こと、解除するとミュートの世代番号つきで送ること、ミュートのランプの後は送らないこと、
// 入力のピークを送信ミュート中も測ることを確かめる。

#include "capture_processor.h"

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/transport/media_payload.h"

#include "fakes/fake_datagram_socket.h"
#include "secure_pair.h"

namespace vocroz::client {
namespace {

constexpr std::size_t kFrame = 240;  // 5ms

class CaptureProcessorTest : public ::testing::Test {
protected:
    CaptureProcessorTest() {
        auto pair = test::make_secure_pair();
        host_ = std::make_unique<transport::SecureChannel>(std::move(pair.host));
        auto socket = std::make_unique<transport::FakeDatagramSocket>();
        socket_ = socket.get();
        sender_.set_path(std::make_unique<SendPath>(
            SendPath{std::move(pair.client.sealer), std::move(socket), 0}));
        sender_.pump();
    }

    // 1 フレーム分の一定値を、128 サンプルのブロックに分けて渡す（デバイスの周期とフレーム長は
    // そろわない）。
    void capture_frames(std::size_t frames, float value = 0.25F) {
        std::vector<float> block(128, value);
        std::size_t remaining = frames * kFrame;
        while (remaining > 0) {
            const std::size_t n = std::min(remaining, block.size());
            processor_.on_captured({std::span<const float>(block).first(n), 1});
            remaining -= n;
        }
        sender_.pump();
    }

    std::vector<transport::MediaPayload> sent_media() {
        std::vector<transport::MediaPayload> out;
        plaintexts_.clear();
        for (const auto& datagram : socket_->datagrams) {
            plaintexts_.emplace_back(transport::kMaxPacketBytes);
            const auto opened = host_->opener.open(datagram, plaintexts_.back()).value();
            out.push_back(transport::decode_media_payload(opened.plaintext).value());
        }
        socket_->datagrams.clear();
        return out;
    }

    SendWakeup wakeup_;
    PacketSender sender_{wakeup_};
    CaptureProcessor processor_{transport::OpusFrameEncoder::create({}).value(), sender_};
    transport::FakeDatagramSocket* socket_ = nullptr;
    std::unique_ptr<transport::SecureChannel> host_;
    std::vector<std::vector<std::uint8_t>> plaintexts_;
};

TEST_F(CaptureProcessorTest, SendsNothingWhileInitiallyMuted) {
    capture_frames(4);
    EXPECT_TRUE(socket_->datagrams.empty());
}

TEST_F(CaptureProcessorTest, SendsFramesWithMuteEpochAfterUnmute) {
    ASSERT_TRUE(processor_.set_params({.muted = false, .mute_epoch = 3, .gain = 1.0F}));
    capture_frames(3);

    const auto media = sent_media();
    ASSERT_EQ(media.size(), 3U);
    for (const auto& m : media) {
        EXPECT_EQ(m.mute_epoch, 3U);
        EXPECT_EQ(m.stream_id, kMicStreamId);
    }
    // タイムスタンプはフレーム長ずつ進む。
    EXPECT_EQ(media[1].timestamp - media[0].timestamp, kFrame);
    EXPECT_EQ(media[2].timestamp - media[1].timestamp, kFrame);
}

TEST_F(CaptureProcessorTest, TimestampsKeepRealTimeAcrossMutedFrames) {
    capture_frames(2);  // ミュート中の 2 フレームは送らない
    ASSERT_TRUE(processor_.set_params({.muted = false, .mute_epoch = 1, .gain = 1.0F}));
    capture_frames(1);

    const auto media = sent_media();
    ASSERT_EQ(media.size(), 1U);
    EXPECT_EQ(media[0].timestamp, 2 * kFrame);
}

TEST_F(CaptureProcessorTest, StopsSendingAfterMuteRampSettles) {
    ASSERT_TRUE(processor_.set_params({.muted = false, .mute_epoch = 1, .gain = 1.0F}));
    capture_frames(2);
    static_cast<void>(sent_media());

    ASSERT_TRUE(processor_.set_params({.muted = true, .mute_epoch = 1, .gain = 1.0F}));
    // ランプ（5ms = 1 フレーム）の間のフレームは送り、終わった後は送らない。
    capture_frames(4);
    EXPECT_LE(sent_media().size(), 1U);
    capture_frames(4);
    EXPECT_TRUE(sent_media().empty());
}

TEST_F(CaptureProcessorTest, MeasuresInputPeakEvenWhileMuted) {
    capture_frames(1, 0.5F);
    EXPECT_FLOAT_EQ(processor_.take_input_peak(), 0.5F);
    // 読むと 0 に戻る。
    EXPECT_FLOAT_EQ(processor_.take_input_peak(), 0.0F);
}

TEST_F(CaptureProcessorTest, UsesFirstChannelOfMultichannelInput) {
    // 2 チャンネルで、1 チャンネル目は 0.1、2 チャンネル目は 0.9。
    std::vector<float> block;
    for (std::size_t i = 0; i < kFrame; ++i) {
        block.push_back(0.1F);
        block.push_back(0.9F);
    }
    processor_.on_captured({block, 2});
    EXPECT_FLOAT_EQ(processor_.take_input_peak(), 0.1F);
}

TEST_F(CaptureProcessorTest, RestartAppliesLatestParamsEvenWhenQueueWasFull) {
    // 入力なしの間に設定が溜まりきった。最後の設定（ミュート）は入らない。
    for (std::uint32_t i = 0; i < 100; ++i) {
        static_cast<void>(processor_.set_params({.muted = false, .mute_epoch = i, .gain = 1.0F}));
    }
    processor_.restart({.muted = true, .mute_epoch = 7, .gain = 1.0F});
    capture_frames(4);
    EXPECT_TRUE(socket_->datagrams.empty());
}

}  // namespace
}  // namespace vocroz::client
