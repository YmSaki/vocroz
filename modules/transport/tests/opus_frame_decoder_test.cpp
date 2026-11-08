// SPDX-License-Identifier: AGPL-3.0-only
//
// OpusFrameDecoder の単体テスト。
// エンコーダーと組み合わせた往復、PLC、入力の検証を確かめる。

#include "vocroz/transport/opus_frame_decoder.h"

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/transport/opus_frame_encoder.h"

namespace vocroz::transport {
namespace {

constexpr std::size_t kFrame = 240;

double rms(const std::vector<float>& samples) {
    double sum = 0.0;
    for (const float s : samples) {
        sum += static_cast<double>(s) * s;
    }
    return std::sqrt(sum / static_cast<double>(samples.size()));
}

// 1kHz、振幅 0.5 の正弦波を frames フレームぶんエンコードしてデコードし、
// デコード結果をつなげて返す。
std::vector<float> round_trip_sine(OpusFrameDecoder& decoder, std::size_t frames) {
    auto encoder = OpusFrameEncoder::create({});
    EXPECT_TRUE(encoder);
    std::vector<float> decoded;
    std::vector<float> pcm(kFrame);
    std::vector<float> out(kFrame);
    std::array<std::uint8_t, kMaxOpusPayloadBytes> packet{};
    for (std::size_t f = 0; f < frames; ++f) {
        for (std::size_t i = 0; i < kFrame; ++i) {
            const double t = static_cast<double>(f * kFrame + i) / kSampleRate;
            pcm[i] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 1000.0 * t));
        }
        const auto bytes = encoder.value().encode(pcm, packet);
        EXPECT_TRUE(bytes);
        const auto samples =
            decoder.decode(std::span<const std::uint8_t>{packet}.first(bytes.value()), out);
        EXPECT_TRUE(samples);
        EXPECT_EQ(samples.value(), kFrame);
        decoded.insert(decoded.end(), out.begin(), out.end());
    }
    return decoded;
}

TEST(OpusFrameDecoderTest, RoundTripKeepsSignalLevel) {
    auto decoder = OpusFrameDecoder::create();
    ASSERT_TRUE(decoder);
    const auto decoded = round_trip_sine(decoder.value(), 100);
    // 先頭はエンコーダーの先読みとデコーダーの立ち上がりで小さいので、後半だけで比べる。
    const std::vector<float> tail(decoded.begin() + 50 * kFrame, decoded.end());
    const double expected = 0.5 / std::numbers::sqrt2;
    EXPECT_NEAR(rms(tail), expected, expected * 0.05);
}

TEST(OpusFrameDecoderTest, ConcealsOneFrameAfterSignal) {
    auto decoder = OpusFrameDecoder::create();
    ASSERT_TRUE(decoder);
    static_cast<void>(round_trip_sine(decoder.value(), 20));
    std::vector<float> out(kFrame);
    const auto samples = decoder.value().conceal(out);
    ASSERT_TRUE(samples);
    EXPECT_EQ(samples.value(), kFrame);
    // 直前まで音があったので、PLC は無音ではなく音を補う。
    EXPECT_GT(rms(out), 0.05);
}

TEST(OpusFrameDecoderTest, RejectsConcealmentNotMultipleOf2500Us) {
    auto decoder = OpusFrameDecoder::create();
    ASSERT_TRUE(decoder);
    std::vector<float> out(100);
    const auto samples = decoder.value().conceal(out);
    ASSERT_FALSE(samples);
    EXPECT_EQ(samples.error(), CodecError::kInvalidFrameSize);
}

TEST(OpusFrameDecoderTest, RejectsEmptyPayload) {
    auto decoder = OpusFrameDecoder::create();
    ASSERT_TRUE(decoder);
    std::vector<float> out(kFrame);
    const auto samples = decoder.value().decode({}, out);
    ASSERT_FALSE(samples);
    EXPECT_EQ(samples.error(), CodecError::kInvalidPacket);
}

TEST(OpusFrameDecoderTest, RejectsFrameLongerThanOutput) {
    auto decoder = OpusFrameDecoder::create();
    ASSERT_TRUE(decoder);
    auto encoder = OpusFrameEncoder::create({});
    ASSERT_TRUE(encoder);
    const std::vector<float> pcm(kFrame, 0.0F);
    std::array<std::uint8_t, kMaxOpusPayloadBytes> packet{};
    const auto bytes = encoder.value().encode(pcm, packet);
    ASSERT_TRUE(bytes);
    std::vector<float> out(120);  // 5ms のフレームに対して 2.5ms ぶんしかない
    const auto samples =
        decoder.value().decode(std::span<const std::uint8_t>{packet}.first(bytes.value()), out);
    EXPECT_FALSE(samples);
}

TEST(OpusFrameDecoderTest, ResetForgetsPreviousSignal) {
    auto decoder = OpusFrameDecoder::create();
    ASSERT_TRUE(decoder);
    static_cast<void>(round_trip_sine(decoder.value(), 20));
    decoder.value().reset();
    std::vector<float> out(kFrame);
    ASSERT_TRUE(decoder.value().conceal(out));
    // 初期化後の PLC は前の区間の音を引き延ばさない。
    EXPECT_LT(rms(out), 1e-4);
}

}  // namespace
}  // namespace vocroz::transport
