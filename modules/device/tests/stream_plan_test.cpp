// SPDX-License-Identifier: AGPL-3.0-only
//
// plan_stream()（ストリームの開き方の判断）の単体テスト。

#include "detail/stream_plan.h"

#include <gtest/gtest.h>

namespace vocroz::device::detail {
namespace {

constexpr EnginePeriod kTypicalPeriod{480, 48, 144, 480};

TEST(StreamPlanTest, UsesLowLatencyWithMinimumPeriodWhenDeviceIsAlready48kFloat) {
    const auto plan = plan_stream(MixFormat{48'000, 2, true}, kTypicalPeriod);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan.value().mode, StreamMode::kLowLatencyShared);
    EXPECT_EQ(plan.value().period_frames, 144U);
}

TEST(StreamPlanTest, ConvertsWhenSampleRateIsNot48k) {
    // 44.1kHz のデバイスは低遅延共有モードでは変換できないので、OS のリサンプリングに任せる。
    const auto plan = plan_stream(MixFormat{44'100, 2, true}, kTypicalPeriod);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan.value().mode, StreamMode::kConvertedShared);
    EXPECT_EQ(plan.value().period_frames, 0U);
}

TEST(StreamPlanTest, ConvertsWhenSamplesAreNotFloat) {
    const auto plan = plan_stream(MixFormat{48'000, 2, false}, kTypicalPeriod);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan.value().mode, StreamMode::kConvertedShared);
}

TEST(StreamPlanTest, ConvertsWhenAudioClient3IsUnavailable) {
    const auto plan = plan_stream(MixFormat{48'000, 2, true}, std::nullopt);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan.value().mode, StreamMode::kConvertedShared);
}

TEST(StreamPlanTest, ConvertsWhenDriverReportsZeroMinimumPeriod) {
    const auto plan = plan_stream(MixFormat{48'000, 2, true}, EnginePeriod{480, 0, 0, 480});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan.value().mode, StreamMode::kConvertedShared);
}

TEST(StreamPlanTest, RejectsZeroOrTooManyChannels) {
    EXPECT_EQ(plan_stream(MixFormat{48'000, 0, true}, kTypicalPeriod).error(),
              audio_core::AudioDeviceError::kUnsupportedFormat);
    EXPECT_EQ(plan_stream(MixFormat{48'000, 65, true}, kTypicalPeriod).error(),
              audio_core::AudioDeviceError::kUnsupportedFormat);
    EXPECT_TRUE(plan_stream(MixFormat{48'000, 64, true}, kTypicalPeriod).has_value());
}

}  // namespace
}  // namespace vocroz::device::detail
