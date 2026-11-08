// SPDX-License-Identifier: AGPL-3.0-only
//
// OpusFrameEncoder の単体テスト。
// 低遅延モードの設定と、フレーム長の検証を確かめる。

#include "vocroz/transport/opus_frame_encoder.h"

#include <array>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::transport {
namespace {

TEST(OpusFrameEncoderTest, EncodesOneFrameOf5Ms) {
    auto encoder = OpusFrameEncoder::create({});
    ASSERT_TRUE(encoder);
    EXPECT_EQ(encoder.value().frame_samples(), 240U);
    const std::vector<float> pcm(240, 0.0F);
    std::array<std::uint8_t, kMaxOpusPayloadBytes> out{};
    const auto bytes = encoder.value().encode(pcm, out);
    ASSERT_TRUE(bytes);
    EXPECT_GT(bytes.value(), 0U);
}

TEST(OpusFrameEncoderTest, EncodesOneFrameOf2500Us) {
    OpusEncoderConfig config;
    config.frame_duration = FrameDuration::k2500Us;
    auto encoder = OpusFrameEncoder::create(config);
    ASSERT_TRUE(encoder);
    const std::vector<float> pcm(120, 0.25F);
    std::array<std::uint8_t, kMaxOpusPayloadBytes> out{};
    EXPECT_TRUE(encoder.value().encode(pcm, out));
}

TEST(OpusFrameEncoderTest, LookaheadIsThatOfRestrictedLowDelayMode) {
    // RESTRICTED_LOWDELAY は SILK の先読みを使わないので、先読みは CELT の 2.5ms（120 サンプル）
    // だけになる。
    auto encoder = OpusFrameEncoder::create({});
    ASSERT_TRUE(encoder);
    EXPECT_EQ(encoder.value().lookahead_samples(), 120U);
}

TEST(OpusFrameEncoderTest, RejectsPcmOfWrongLength) {
    auto encoder = OpusFrameEncoder::create({});
    ASSERT_TRUE(encoder);
    const std::vector<float> pcm(120, 0.0F);
    std::array<std::uint8_t, kMaxOpusPayloadBytes> out{};
    const auto bytes = encoder.value().encode(pcm, out);
    ASSERT_FALSE(bytes);
    EXPECT_EQ(bytes.error(), CodecError::kInvalidFrameSize);
}

TEST(OpusFrameEncoderTest, RejectsEmptyOutput) {
    auto encoder = OpusFrameEncoder::create({});
    ASSERT_TRUE(encoder);
    const std::vector<float> pcm(240, 0.0F);
    const auto bytes = encoder.value().encode(pcm, {});
    ASSERT_FALSE(bytes);
    EXPECT_EQ(bytes.error(), CodecError::kBufferTooSmall);
}

TEST(OpusFrameEncoderTest, RejectsBitrateOutOfRange) {
    OpusEncoderConfig config;
    config.bitrate_bps = -5;
    const auto encoder = OpusFrameEncoder::create(config);
    ASSERT_FALSE(encoder);
    EXPECT_EQ(encoder.error(), CodecError::kCreateFailed);
}

}  // namespace
}  // namespace vocroz::transport
