// SPDX-License-Identifier: AGPL-3.0-only
//
// common のポートのフェイク（FakeClock、FakeLogger）の単体テスト。
// 他モジュールのテストがこれらに依存するので、フェイク自体が決定的に動くことを確かめる。

#include <chrono>

#include <gtest/gtest.h>

#include "fakes/fake_clock.h"
#include "fakes/fake_logger.h"

namespace vocroz::common {
namespace {

using std::chrono_literals::operator""ms;

TEST(FakeClockTest, AdvancesOnlyWhenRequested) {
    FakeClock clock;
    const Clock::TimePoint start = clock.now();
    EXPECT_EQ(clock.now(), start);
    clock.advance(250ms);
    EXPECT_EQ(clock.now() - start, 250ms);
}

TEST(FakeLoggerTest, RecordsEntriesInOrder) {
    FakeLogger logger;
    Logger& port = logger;
    port.log(LogLevel::kInfo, "connected");
    port.log(LogLevel::kError, "lost");

    const auto entries = logger.entries();
    ASSERT_EQ(entries.size(), 2U);
    EXPECT_EQ(entries[0].level, LogLevel::kInfo);
    EXPECT_EQ(entries[0].message, "connected");
    EXPECT_EQ(entries[1].level, LogLevel::kError);
    EXPECT_EQ(entries[1].message, "lost");
}

}  // namespace
}  // namespace vocroz::common
