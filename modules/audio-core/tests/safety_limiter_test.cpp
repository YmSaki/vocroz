// SPDX-License-Identifier: AGPL-3.0-only
//
// SafetyLimiter の単体テスト。天井を超えないこと、小さい信号を変えないこと、不正な値の除去、
// リリースで戻ることを検証する。

#include "safety_limiter.h"

#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/audio_core/constants.h"

namespace vocroz::audio_core {
namespace {

// -1 dBFS（audio_core_constants.h の kLimiterCeiling）。
constexpr float kCeiling = 0.891F;

void process_stereo(SafetyLimiter& limiter, std::vector<float>& left, std::vector<float>& right) {
    std::array<float*, 2> channels{left.data(), right.data()};
    limiter.process(channels.data(), 2, left.size());
}

TEST(SafetyLimiterTest, PassesSignalBelowCeilingUnchanged) {
    SafetyLimiter limiter;
    std::vector<float> left(kBlockSize, 0.5F);
    std::vector<float> right(kBlockSize, -0.5F);
    process_stereo(limiter, left, right);
    EXPECT_EQ(left, std::vector<float>(kBlockSize, 0.5F));
    EXPECT_EQ(right, std::vector<float>(kBlockSize, -0.5F));
}

TEST(SafetyLimiterTest, NeverExceedsCeilingEvenOnFirstSample) {
    SafetyLimiter limiter;
    std::vector<float> left(kBlockSize, 4.0F);
    std::vector<float> right(kBlockSize, -8.0F);
    process_stereo(limiter, left, right);
    for (std::size_t i = 0; i < kBlockSize; ++i) {
        EXPECT_LE(std::fabs(left[i]), kCeiling + 1e-6F);
        EXPECT_LE(std::fabs(right[i]), kCeiling + 1e-6F);
    }
    // チャンネル間でゲインを連動させるので、L と R の比は保たれる。
    EXPECT_FLOAT_EQ(right[0] / left[0], -2.0F);
}

TEST(SafetyLimiterTest, ReplacesNonFiniteSamplesWithSilence) {
    SafetyLimiter limiter;
    std::vector<float> left{std::numeric_limits<float>::quiet_NaN(), 0.1F};
    std::vector<float> right{std::numeric_limits<float>::infinity(), 0.1F};
    process_stereo(limiter, left, right);
    EXPECT_EQ(left[0], 0.0F);
    EXPECT_EQ(right[0], 0.0F);
    EXPECT_FLOAT_EQ(left[1], 0.1F);  // 不正な値でゲインの状態が壊れない
}

TEST(SafetyLimiterTest, ReleasesBackToUnityAfterLoudSignal) {
    SafetyLimiter limiter;
    std::vector<float> loud_left(kBlockSize, 2.0F);
    std::vector<float> loud_right(kBlockSize, 2.0F);
    process_stereo(limiter, loud_left, loud_right);
    EXPECT_LT(limiter.gain(), 0.5F);

    // リリース時間 100ms の 10 倍（1 秒）たてば、ほぼ 1 に戻る。
    std::vector<float> quiet_left(kSampleRate, 0.1F);
    std::vector<float> quiet_right(kSampleRate, 0.1F);
    process_stereo(limiter, quiet_left, quiet_right);
    EXPECT_GT(limiter.gain(), 0.999F);
}

}  // namespace
}  // namespace vocroz::audio_core
