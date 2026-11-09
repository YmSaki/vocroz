// SPDX-License-Identifier: AGPL-3.0-only
//
// SteadyClock の単体テスト。

#include "vocroz/common/steady_clock.h"

#include <gtest/gtest.h>

namespace vocroz::common {
namespace {

// 単調増加の約束（前回より前の時刻を返さない）を、連続した呼び出しで確かめる。
TEST(SteadyClockTest, NeverGoesBackwards) {
    const SteadyClock clock;
    Clock::TimePoint previous = clock.now();
    for (int i = 0; i < 1000; ++i) {
        const Clock::TimePoint current = clock.now();
        EXPECT_GE(current, previous);
        previous = current;
    }
}

}  // namespace
}  // namespace vocroz::common
