// SPDX-License-Identifier: AGPL-3.0-only
//
// request_id の重複の判定のテスト。

#include "request_deduplicator.h"

#include <gtest/gtest.h>

namespace vocroz::session {
namespace {

TEST(RequestDeduplicatorTest, ReportsARepeatedIdAsDuplicate) {
    RequestDeduplicator dedup{4};
    EXPECT_TRUE(dedup.remember("r1"));
    EXPECT_TRUE(dedup.remember("r2"));
    EXPECT_FALSE(dedup.remember("r1"));
}

TEST(RequestDeduplicatorTest, ForgetsTheOldestIdBeyondCapacity) {
    RequestDeduplicator dedup{2};
    EXPECT_TRUE(dedup.remember("r1"));
    EXPECT_TRUE(dedup.remember("r2"));
    EXPECT_TRUE(dedup.remember("r3"));
    EXPECT_TRUE(dedup.remember("r1"));
    EXPECT_FALSE(dedup.remember("r3"));
}

}  // namespace
}  // namespace vocroz::session
