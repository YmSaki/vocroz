// SPDX-License-Identifier: AGPL-3.0-only
//
// FreeSpaceGuard のテスト。空き容量を返す関数を差し替え、調べる間隔と下限の判断を確かめる。

#include "detail/free_space_guard.h"

#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

namespace vocroz::media::detail {
namespace {

constexpr std::uint64_t kMinFree = 1000;
constexpr std::uint64_t kInterval = 100;

struct FakeVolume {
    std::optional<std::uint64_t> free;
    int queries = 0;

    FreeSpaceGuard::FreeBytesQuery query() {
        return [this] {
            ++queries;
            return free;
        };
    }
};

TEST(FreeSpaceGuardTest, ChecksOnFirstWriteAndThenEveryInterval) {
    FakeVolume volume{10'000};
    FreeSpaceGuard guard(volume.query(), kMinFree, kInterval);
    EXPECT_TRUE(guard.admit(40));
    EXPECT_EQ(volume.queries, 1);
    EXPECT_TRUE(guard.admit(40));
    EXPECT_EQ(volume.queries, 1);
    // 前に調べてから 80 バイト書いたので、次の 40 バイトで間隔に達する。
    EXPECT_TRUE(guard.admit(40));
    EXPECT_EQ(volume.queries, 2);
}

TEST(FreeSpaceGuardTest, RefusesWriteThatWouldGoBelowMinimum) {
    FakeVolume volume{kMinFree + 50};
    FreeSpaceGuard guard(volume.query(), kMinFree, kInterval);
    EXPECT_TRUE(guard.admit(30));
    // 調べた空きから書いた量を引いた見積もりで、下限を下回る書き出しを断る。
    EXPECT_FALSE(guard.admit(30));
}

TEST(FreeSpaceGuardTest, RefusesWhenVolumeAlreadyBelowMinimum) {
    FakeVolume volume{kMinFree - 1};
    FreeSpaceGuard guard(volume.query(), kMinFree, kInterval);
    EXPECT_FALSE(guard.admit(1));
    EXPECT_TRUE(guard.is_below_minimum());
}

TEST(FreeSpaceGuardTest, AllowsWritesWhenFreeSpaceIsUnknown) {
    FakeVolume volume{std::nullopt};
    FreeSpaceGuard guard(volume.query(), kMinFree, kInterval);
    EXPECT_TRUE(guard.admit(1'000'000));
    EXPECT_FALSE(guard.is_below_minimum());
}

TEST(FreeSpaceGuardTest, AllowsWritesWithoutQuery) {
    FreeSpaceGuard guard({}, kMinFree, kInterval);
    EXPECT_TRUE(guard.admit(1'000'000));
    EXPECT_FALSE(guard.is_below_minimum());
}

TEST(FreeSpaceGuardTest, IsBelowMinimumQueriesImmediately) {
    FakeVolume volume{10'000};
    FreeSpaceGuard guard(volume.query(), kMinFree, kInterval);
    EXPECT_FALSE(guard.is_below_minimum());
    volume.free = 10;
    EXPECT_TRUE(guard.is_below_minimum());
    EXPECT_EQ(volume.queries, 2);
}

}  // namespace
}  // namespace vocroz::media::detail
