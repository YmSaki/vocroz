// SPDX-License-Identifier: AGPL-3.0-only
//
// DriftResampler の単体テスト。合成した正弦波を可変レートで変換し、
// 理論上の波形との差（SNR）と、入力の消費量が変換比どおりであることを数値で確かめる。

#include "drift_resampler.h"

#include <cmath>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/transport/constants.h"

namespace vocroz::transport {
namespace {

constexpr std::size_t kChunk = 240;

// frequency の正弦波を ratio で変換し、出力と理論値の SNR（dB）を返す。
double resampled_sine_snr_db(double frequency, double ratio) {
    DriftResampler resampler(kChunk);
    const double omega = 2.0 * std::numbers::pi * frequency / kSampleRate;
    std::vector<float> chunk(kChunk);
    std::vector<float> out(64);
    std::size_t pushed = 0;
    std::size_t produced = 0;
    double signal = 0.0;
    double noise = 0.0;
    while (produced < 48000) {
        const std::size_t n = resampler.pull(out, ratio);
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t k = produced + i;
            // 出力 k は入力の位置 k * ratio
            // に対応する。先頭は過去を無音とみなすので、フィルターの長さぶん除く。
            if (k >= 256) {
                const double expected = 0.5 * std::sin(omega * static_cast<double>(k) * ratio);
                signal += expected * expected;
                noise += (out[i] - expected) * (out[i] - expected);
            }
        }
        produced += n;
        if (n < out.size()) {
            for (std::size_t i = 0; i < kChunk; ++i) {
                chunk[i] =
                    static_cast<float>(0.5 * std::sin(omega * static_cast<double>(pushed + i)));
            }
            pushed += kChunk;
            EXPECT_TRUE(resampler.push(chunk));
        }
    }
    return 10.0 * std::log10(signal / noise);
}

TEST(DriftResamplerTest, UnityRatioKeepsTimingOfInput) {
    // 変換比 1 では、出力 k は入力 k と同じ時刻になる（片側タップぶんの遅延は FIFO
    // の先読みとして現れ、 出力の時刻はずれない）。帯域内の正弦波は誤差 -70dB 未満で一致する。
    DriftResampler resampler(kChunk);
    std::vector<float> input(kChunk);
    const double omega = 2.0 * std::numbers::pi * 1000.0 / kSampleRate;
    for (std::size_t i = 0; i < kChunk; ++i) {
        input[i] = static_cast<float>(0.5 * std::sin(omega * static_cast<double>(i)));
    }
    ASSERT_TRUE(resampler.push(input));
    std::vector<float> out(kChunk);
    const std::size_t n = resampler.pull(out, 1.0);
    // 末尾の片側タップぶんは先読みが足りないので出力されない。
    ASSERT_EQ(n, kChunk - DriftResampler::kHalfTaps);
    // 先頭は過去を無音とみなしているので、フィルターの長さぶんを除いて比べる。
    for (std::size_t i = 2 * DriftResampler::kHalfTaps; i < n; ++i) {
        EXPECT_NEAR(out[i], input[i], 1.6e-4F);
    }
}

TEST(DriftResamplerTest, KeepsHighSnrAt1kHzWithDriftCorrection) {
    EXPECT_GT(resampled_sine_snr_db(1000.0, 1.0 + 100e-6), 70.0);
    EXPECT_GT(resampled_sine_snr_db(1000.0, 1.0 - 2e-3), 70.0);
}

TEST(DriftResamplerTest, KeepsHighSnrAt10kHzWithDriftCorrection) {
    // 70dB は 16 ビットの量子化雑音（約 98dB）には届かないが、Opus の符号化雑音よりずっと小さい。
    EXPECT_GT(resampled_sine_snr_db(10000.0, 1.0 + 2e-3), 70.0);
}

TEST(DriftResamplerTest, KeepsUsableSnrAt18kHz) {
    // 高域ほど窓の影響を受けるが、Opus の帯域の上限付近でも誤差は聞こえない水準にとどめる。
    EXPECT_GT(resampled_sine_snr_db(18000.0, 1.0 + 2e-3), 50.0);
}

TEST(DriftResamplerTest, ConsumesInputAtTheGivenRatio) {
    DriftResampler resampler(kChunk);
    const std::vector<float> chunk(kChunk, 0.1F);
    std::vector<float> out(100);
    const double ratio = 1.002;
    std::size_t pushed = 0;
    std::size_t produced = 0;
    while (produced < 100000) {
        const std::size_t n = resampler.pull(out, ratio);
        produced += n;
        if (n < out.size()) {
            ASSERT_TRUE(resampler.push(chunk));
            pushed += kChunk;
        }
    }
    const double consumed = static_cast<double>(pushed) - resampler.buffered_samples();
    EXPECT_NEAR(consumed / static_cast<double>(produced), ratio, 1e-4);
}

TEST(DriftResamplerTest, RejectsPushBeyondCapacity) {
    DriftResampler resampler(kChunk);
    const std::vector<float> chunk(kChunk, 0.0F);
    int accepted = 0;
    while (resampler.push(chunk) && accepted < 100) {
        ++accepted;
    }
    EXPECT_LT(accepted, 100);
}

TEST(DriftResamplerTest, ResetTreatsHistoryAsSilence) {
    DriftResampler resampler(kChunk);
    const std::vector<float> loud(kChunk, 1.0F);
    ASSERT_TRUE(resampler.push(loud));
    resampler.reset();
    EXPECT_EQ(resampler.buffered_samples(), 0.0);
    const std::vector<float> quiet(kChunk, 0.0F);
    ASSERT_TRUE(resampler.push(quiet));
    std::vector<float> out(64);
    ASSERT_EQ(resampler.pull(out, 1.0), out.size());
    for (const float s : out) {
        EXPECT_EQ(s, 0.0F);
    }
}

}  // namespace
}  // namespace vocroz::transport
