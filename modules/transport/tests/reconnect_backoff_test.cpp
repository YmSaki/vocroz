// SPDX-License-Identifier: AGPL-3.0-only
//
// 再接続のバックオフの単体テスト。間隔が 1、2、4、8、15 秒で進み、ジッターが ±20% に収まることを、
// 乱数をフェイクに差し替えて確かめる。

#include "vocroz/transport/reconnect_backoff.h"

#include <chrono>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "fakes/fake_random_source.h"

namespace vocroz::transport {
namespace {

using std::chrono::milliseconds;

// 乱数の中央値。ジッターがちょうど 0 になる。
constexpr std::uint32_t kMiddle = 0x80000000U;

TEST(ReconnectBackoffTest, DelaysFollowScheduleAndStayAtFifteenSeconds) {
    FakeRandomSource random;
    random.value = kMiddle;
    ReconnectBackoff backoff(random);

    for (const auto expected : {milliseconds{1000}, milliseconds{2000}, milliseconds{4000},
                                milliseconds{8000}, milliseconds{15000}, milliseconds{15000}}) {
        EXPECT_EQ(backoff.next_delay(), expected);
    }
    EXPECT_EQ(backoff.attempts(), 6U);
    EXPECT_EQ(random.calls, 6);
}

TEST(ReconnectBackoffTest, ResetStartsFromOneSecondAgain) {
    FakeRandomSource random;
    random.value = kMiddle;
    ReconnectBackoff backoff(random);
    static_cast<void>(backoff.next_delay());
    static_cast<void>(backoff.next_delay());

    backoff.reset();
    EXPECT_EQ(backoff.attempts(), 0U);
    EXPECT_EQ(backoff.next_delay(), milliseconds{1000});
}

TEST(ReconnectBackoffTest, JitterSpansMinusToPlusTwentyPercent) {
    EXPECT_EQ(reconnect_delay(0, 0), milliseconds{800});
    EXPECT_EQ(reconnect_delay(0, std::numeric_limits<std::uint32_t>::max()), milliseconds{1200});
    EXPECT_EQ(reconnect_delay(4, 0), milliseconds{12000});
    EXPECT_EQ(reconnect_delay(4, std::numeric_limits<std::uint32_t>::max()), milliseconds{18000});
}

TEST(ReconnectBackoffTest, JitterStaysWithinRangeForAnyRandomValue) {
    for (std::uint64_t value = 0; value <= std::numeric_limits<std::uint32_t>::max();
         value += 0x01000193U) {
        const auto delay = reconnect_delay(2, static_cast<std::uint32_t>(value));
        EXPECT_GE(delay, milliseconds{3200});
        EXPECT_LE(delay, milliseconds{4800});
    }
}

}  // namespace
}  // namespace vocroz::transport
