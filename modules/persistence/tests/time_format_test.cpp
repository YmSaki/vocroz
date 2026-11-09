// SPDX-License-Identifier: AGPL-3.0-only
//
// format_utc_timestamp() のテスト。

#include "detail/time_format.h"

#include <gtest/gtest.h>

namespace vocroz::persistence::detail {
namespace {

TEST(TimeFormatTest, FormatsUtcWithMilliseconds) {
    using namespace std::chrono;
    const sys_days day = 2026y / November / 8;
    const auto time = day + 4h + 44min + 52s + 123ms;
    EXPECT_EQ(format_utc_timestamp(time), "2026-11-08T04:44:52.123Z");
}

TEST(TimeFormatTest, PadsSmallFields) {
    using namespace std::chrono;
    const sys_days day = 2027y / January / 2;
    EXPECT_EQ(format_utc_timestamp(day + 3s + 4ms), "2027-01-02T00:00:03.004Z");
}

}  // namespace
}  // namespace vocroz::persistence::detail
