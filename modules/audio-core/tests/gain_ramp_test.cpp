// SPDX-License-Identifier: AGPL-3.0-only
//
// GainRamp の単体テスト。初期ミュート、ランプの長さと単調性、途中での目標変更を検証する。

#include "vocroz/audio_core/gain_ramp.h"

#include <array>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::audio_core {
namespace {

TEST(GainRampTest, StartsSilent) {
    GainRamp ramp(4);
    EXPECT_EQ(ramp.current(), 0.0F);
    EXPECT_TRUE(ramp.is_settled());
}

TEST(GainRampTest, ReachesTargetAfterRampSamplesMonotonically) {
    GainRamp ramp(4);
    ramp.set_target(1.0F);
    std::vector<float> gains;
    for (int i = 0; i < 6; ++i) {
        gains.push_back(ramp.next());
    }
    // 1 サンプルで跳ばず、4 サンプルかけて 0 から 1 へ上がる。
    EXPECT_EQ(gains[0], 0.0F);
    EXPECT_FLOAT_EQ(gains[1], 0.25F);
    EXPECT_FLOAT_EQ(gains[2], 0.5F);
    EXPECT_FLOAT_EQ(gains[3], 0.75F);
    EXPECT_EQ(gains[4], 1.0F);
    EXPECT_EQ(gains[5], 1.0F);
    EXPECT_TRUE(ramp.is_settled());
}

TEST(GainRampTest, RetargetsFromCurrentValueDuringRamp) {
    GainRamp ramp(4);
    ramp.set_target(1.0F);
    (void)ramp.next();
    (void)ramp.next();  // 現在値 0.5
    ramp.set_target(0.0F);
    EXPECT_FLOAT_EQ(ramp.next(), 0.5F);
    EXPECT_FLOAT_EQ(ramp.next(), 0.375F);
}

TEST(GainRampTest, JumpToSkipsRamp) {
    GainRamp ramp(480);
    ramp.set_target(1.0F);
    ramp.jump_to(0.0F);
    EXPECT_TRUE(ramp.is_settled());
    EXPECT_EQ(ramp.next(), 0.0F);
}

TEST(GainRampTest, ApplyMultipliesAllChannelsAndSilencesWhenSettledAtZero) {
    GainRamp ramp(2);
    std::array<float, 4> left{1.0F, 1.0F, 1.0F, 1.0F};
    std::array<float, 4> right{2.0F, 2.0F, 2.0F, 2.0F};
    std::array<float*, 2> channels{left.data(), right.data()};
    ramp.set_target(1.0F);
    ramp.apply(channels.data(), 2, 4);
    EXPECT_EQ(left, (std::array<float, 4>{0.0F, 0.5F, 1.0F, 1.0F}));
    EXPECT_EQ(right, (std::array<float, 4>{0.0F, 1.0F, 2.0F, 2.0F}));

    // 無音で落ち着いた後は、不正な値（NaN）も 0 で上書きする。
    ramp.jump_to(0.0F);
    std::array<float, 2> noisy{std::numeric_limits<float>::quiet_NaN(), 1.0F};
    std::array<float*, 1> mono{noisy.data()};
    ramp.apply(mono.data(), 1, 2);
    EXPECT_EQ(noisy, (std::array<float, 2>{0.0F, 0.0F}));
}

TEST(GainRampTest, TreatsZeroLengthAsOneSample) {
    GainRamp ramp(0);
    ramp.set_target(1.0F);
    EXPECT_EQ(ramp.next(), 0.0F);
    EXPECT_EQ(ramp.next(), 1.0F);
}

}  // namespace
}  // namespace vocroz::audio_core
