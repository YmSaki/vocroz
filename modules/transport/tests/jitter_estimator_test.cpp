// SPDX-License-Identifier: AGPL-3.0-only
//
// 到着間隔のジッターの推定のテスト。

#include "vocroz/transport/jitter_estimator.h"

#include <gtest/gtest.h>

namespace vocroz::transport {
namespace {

using namespace std::chrono_literals;
using TimePoint = JitterEstimator::TimePoint;

// 5ms のフレーム（48kHz で 240 サンプル）。
constexpr std::uint64_t kFrame = 240;

TEST(JitterEstimatorTest, HasNoEstimateUntilTwoFramesArrive) {
    JitterEstimator estimator;
    EXPECT_FALSE(estimator.jitter());
    estimator.on_frame(0, 1, TimePoint{});
    EXPECT_FALSE(estimator.jitter());
}

TEST(JitterEstimatorTest, IsZeroWhenFramesArriveExactlyOnTime) {
    JitterEstimator estimator;
    for (std::uint64_t i = 0; i < 50; ++i) {
        estimator.on_frame(i * kFrame, 1, TimePoint{} + i * 5ms);
    }
    EXPECT_EQ(estimator.jitter(), 0us);
}

TEST(JitterEstimatorTest, ConvergesToTheAlternatingDeviation) {
    // 到着の間隔が 3ms と 7ms を交互にとると、メディアの 5ms との差は毎回 2ms。
    JitterEstimator estimator;
    TimePoint arrival{};
    for (std::uint64_t i = 0; i < 400; ++i) {
        estimator.on_frame(i * kFrame, 1, arrival);
        arrival += (i % 2 == 0) ? 3ms : 7ms;
    }
    const auto jitter = estimator.jitter();
    ASSERT_TRUE(jitter);
    EXPECT_NEAR(static_cast<double>(jitter->count()), 2000.0, 1.0);
}

TEST(JitterEstimatorTest, RestartsTheBaselineWhenTheMuteEpochChanges) {
    JitterEstimator estimator;
    estimator.on_frame(0, 1, TimePoint{});
    estimator.on_frame(kFrame, 1, TimePoint{} + 5ms);
    // ミュートの解除で送信が再開した。タイムスタンプも到着も離れているが、ゆらぎではない。
    estimator.on_frame(100 * kFrame, 2, TimePoint{} + 10s);
    estimator.on_frame(101 * kFrame, 2, TimePoint{} + 10s + 5ms);
    EXPECT_EQ(estimator.jitter(), 0us);
}

TEST(JitterEstimatorTest, CountsReorderedFramesAsJitter) {
    JitterEstimator estimator;
    estimator.on_frame(0, 1, TimePoint{});
    // 2 番目のフレームが 3 番目より後に届いた。
    estimator.on_frame(2 * kFrame, 1, TimePoint{} + 10ms);
    estimator.on_frame(kFrame, 1, TimePoint{} + 11ms);
    const auto jitter = estimator.jitter();
    ASSERT_TRUE(jitter);
    EXPECT_GT(jitter->count(), 0);
}

}  // namespace
}  // namespace vocroz::transport
