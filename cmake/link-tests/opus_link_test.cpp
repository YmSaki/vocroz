// SPDX-License-Identifier: AGPL-3.0-only
//
// libopus のリンク確認テスト。低遅延モードで 1
// フレームをエンコードしてデコードできることを確かめる。

#include <array>

#include <gtest/gtest.h>
#include <opus.h>

namespace {

// 低遅延モードで使う設定（docs/adr/0002-opus-low-delay.md）。48kHz モノラル、フレーム長 2.5ms。
constexpr opus_int32 kSampleRate = 48000;
constexpr int kChannels = 1;
constexpr int kFrameSamples = 120;  // 48kHz で 2.5ms
// 1 フレームのエンコード結果の上限。Opus の 1 パケットの最大長（1275 バイト）より大きい値にする。
constexpr opus_int32 kMaxPacketBytes = 1500;

TEST(OpusLinkTest, EncodesAndDecodesOneFrameInRestrictedLowDelayMode) {
    int error = OPUS_OK;
    OpusEncoder* encoder =
        opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &error);
    ASSERT_EQ(error, OPUS_OK);
    OpusDecoder* decoder = opus_decoder_create(kSampleRate, kChannels, &error);
    ASSERT_EQ(error, OPUS_OK);

    const std::array<float, kFrameSamples> input{};
    std::array<unsigned char, kMaxPacketBytes> packet{};
    const opus_int32 packet_bytes =
        opus_encode_float(encoder, input.data(), kFrameSamples, packet.data(), kMaxPacketBytes);
    EXPECT_GT(packet_bytes, 0);

    std::array<float, kFrameSamples> output{};
    const int decoded =
        opus_decode_float(decoder, packet.data(), packet_bytes, output.data(), kFrameSamples, 0);
    EXPECT_EQ(decoded, kFrameSamples);

    opus_decoder_destroy(decoder);
    opus_encoder_destroy(encoder);
}

}  // namespace
