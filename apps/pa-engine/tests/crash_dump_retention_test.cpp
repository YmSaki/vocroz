// SPDX-License-Identifier: AGPL-3.0-only
//
// select_stale_crash_dumps() の単体テスト。

#include "crash_dump_retention.h"

#include <gtest/gtest.h>

namespace vocroz::engine {
namespace {

TEST(CrashDumpRetentionTest, SelectsOldestDumpsBeyondTheLimit) {
    const std::vector<std::string> names{
        "pa-engine-20261108-100000-1.dmp", "pa-engine-20261109-090000-2.dmp",
        "pa-engine-20261107-235959-3.dmp", "pa-engine-20261109-120000-4.dmp"};
    EXPECT_EQ(select_stale_crash_dumps(names, 2),
              (std::vector<std::string>{"pa-engine-20261108-100000-1.dmp",
                                        "pa-engine-20261107-235959-3.dmp"}));
}

TEST(CrashDumpRetentionTest, KeepsEverythingWithinTheLimit) {
    EXPECT_TRUE(select_stale_crash_dumps({"pa-engine-20261108-100000-1.dmp"}, 4).empty());
}

// 利用者が置いたファイルや、ほかのプロセスのダンプは消さない。
TEST(CrashDumpRetentionTest, IgnoresFilesThatAreNotEngineDumps) {
    const std::vector<std::string> names{"notes.txt", "remote-pa-20261101-000000-1.dmp",
                                         "pa-engine-.dmp", "pa-engine-20261109-120000-4.dmp"};
    EXPECT_TRUE(select_stale_crash_dumps(names, 0) ==
                std::vector<std::string>{"pa-engine-20261109-120000-4.dmp"});
}

}  // namespace
}  // namespace vocroz::engine
