// SPDX-License-Identifier: AGPL-3.0-only
//
// MediaStreamEncoder の単体テスト。初期ミュートで送らないこと、解除するとフレーム長ごとに送ること、
// ミュートするとランプの後で送信が止まること、止めている間もタイムスタンプが進むこと、
// 世代番号を載せること、送信待ちの満杯で捨てたフレームを数えることを確かめる。

#include "vocroz/transport/media_stream_encoder.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/transport/opus_frame_decoder.h"

#include "fakes/fake_media_frame_sink.h"

namespace vocroz::transport {
namespace {

// 5ms のフレーム。
constexpr std::size_t kFrame = 240;
// デバイスのコールバック長（フレーム長とそろえない）。
constexpr std::size_t kBlock = 128;

class MediaStreamEncoderTest : public ::testing::Test {
protected:
    MediaStreamEncoderTest() : encoder_(make_encoder(), sink_) {}

    static OpusFrameEncoder make_encoder() {
        auto created = OpusFrameEncoder::create({});
        EXPECT_TRUE(created);
        return std::move(created).value();
    }

    // samples サンプルぶんを kBlock ずつ渡す。
    void feed(std::size_t samples, float value = 0.25F) {
        const std::vector<float> block(kBlock, value);
        while (samples > 0) {
            const std::size_t n = std::min(samples, kBlock);
            encoder_.push(std::span<const float>(block).first(n));
            samples -= n;
        }
    }

    void unmute(std::uint32_t epoch) {
        ASSERT_TRUE(encoder_.set_params({false, epoch, 1.0F}));
    }

    void mute(std::uint32_t epoch) {
        ASSERT_TRUE(encoder_.set_params({true, epoch, 1.0F}));
    }

    FakeMediaFrameSink sink_;
    MediaStreamEncoder encoder_;
};

TEST_F(MediaStreamEncoderTest, SendsNothingWhileInitiallyMuted) {
    feed(kFrame * 10);
    EXPECT_TRUE(sink_.frames().empty());
    EXPECT_EQ(encoder_.dropped_frames(), 0U);
}

TEST_F(MediaStreamEncoderTest, SendsOneFramePerFrameLengthAfterUnmute) {
    unmute(1);
    feed(kFrame * 4);
    const auto frames = sink_.frames();
    ASSERT_EQ(frames.size(), 4U);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        EXPECT_EQ(frames[i].timestamp, i * kFrame);
        EXPECT_EQ(frames[i].mute_epoch, 1U);
        EXPECT_GT(frames[i].payload_size, 0U);
    }
}

TEST_F(MediaStreamEncoderTest, FramesDecodeAsOpus) {
    unmute(1);
    feed(kFrame);
    ASSERT_EQ(sink_.frames().size(), 1U);
    auto decoder = OpusFrameDecoder::create();
    ASSERT_TRUE(decoder);
    std::vector<float> pcm(kFrame);
    EXPECT_TRUE(decoder.value().decode(sink_.frames()[0].payload_bytes(), pcm));
}

TEST_F(MediaStreamEncoderTest, StopsSendingAfterTheMuteRamp) {
    unmute(1);
    feed(kFrame * 2);
    mute(1);
    feed(kFrame * 10);
    // ランプ（5ms）が終わるまでのフレームだけを送り、以後は送らない。
    const std::size_t sent = sink_.frames().size();
    EXPECT_LE(sent, 2U + kStreamMuteRampSamples / kFrame + 1U);
    feed(kFrame * 10);
    EXPECT_EQ(sink_.frames().size(), sent);
}

TEST_F(MediaStreamEncoderTest, TimestampsKeepAdvancingWhileMuted) {
    feed(kFrame * 3);
    unmute(1);
    feed(kFrame);
    ASSERT_EQ(sink_.frames().size(), 1U);
    // ミュート中の 3 フレームぶんも実時間どおりに進んでいる。
    EXPECT_EQ(sink_.frames()[0].timestamp, 3 * kFrame);
}

TEST_F(MediaStreamEncoderTest, FramesAfterResumeCarryTheNewEpoch) {
    unmute(1);
    feed(kFrame);
    mute(1);
    feed(kFrame * 4);
    sink_.clear();
    unmute(2);
    feed(kFrame * 2);
    ASSERT_FALSE(sink_.frames().empty());
    for (const auto& frame : sink_.frames()) {
        EXPECT_EQ(frame.mute_epoch, 2U);
    }
}

TEST_F(MediaStreamEncoderTest, CountsFramesDroppedBecauseTheSinkIsFull) {
    unmute(1);
    sink_.set_full(true);
    feed(kFrame * 3);
    EXPECT_EQ(encoder_.dropped_frames(), 3U);
}

TEST_F(MediaStreamEncoderTest, FadesInAfterUnmute) {
    // 解除の直後はランプで立ち上げ、クリックノイズを出さない。最初のフレームの先頭は小さい。
    unmute(1);
    feed(kFrame, 0.5F);
    ASSERT_EQ(sink_.frames().size(), 1U);
    auto decoder = OpusFrameDecoder::create();
    ASSERT_TRUE(decoder);
    std::vector<float> pcm(kFrame);
    ASSERT_TRUE(decoder.value().decode(sink_.frames()[0].payload_bytes(), pcm));
    EXPECT_LT(std::abs(pcm[0]), 0.1F);
}

}  // namespace
}  // namespace vocroz::transport
