// SPDX-License-Identifier: AGPL-3.0-only
//
// fader_db_to_linear() と linear_to_db() の単体テスト。

#include "vocroz/audio_core/gain.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "vocroz/audio_core/constants.h"

namespace vocroz::audio_core {
namespace {

constexpr float kInfinity = std::numeric_limits<float>::infinity();

TEST(GainTest, ConvertsZeroDbToUnity) {
    EXPECT_FLOAT_EQ(fader_db_to_linear(0.0F), 1.0F);
}

TEST(GainTest, ConvertsMinusSixDbToAboutHalf) {
    EXPECT_NEAR(fader_db_to_linear(-6.0206F), 0.5F, 1e-4F);
}

TEST(GainTest, TreatsMinusInfinityAndNanAsSilence) {
    EXPECT_EQ(fader_db_to_linear(-kInfinity), 0.0F);
    EXPECT_EQ(fader_db_to_linear(std::numeric_limits<float>::quiet_NaN()), 0.0F);
}

TEST(GainTest, ClampsAboveFaderMaximum) {
    const float maximum = fader_db_to_linear(kMaxFaderGainDb);
    EXPECT_FLOAT_EQ(fader_db_to_linear(40.0F), maximum);
    EXPECT_FLOAT_EQ(fader_db_to_linear(kInfinity), maximum);
}

TEST(GainTest, ConvertsLinearBackToDb) {
    EXPECT_NEAR(linear_to_db(fader_db_to_linear(-12.5F)), -12.5F, 1e-4F);
    EXPECT_EQ(linear_to_db(0.0F), -kInfinity);
    EXPECT_EQ(linear_to_db(-1.0F), -kInfinity);
}

}  // namespace
}  // namespace vocroz::audio_core
