// SPDX-License-Identifier: AGPL-3.0-only
//
// vocroz::common の引数ユーティリティの単体テスト。

#include "vocroz/common/cli.h"

#include <gtest/gtest.h>

namespace vocroz::common {
namespace {

TEST(MakeArgsTest, ExcludesProgramName) {
    const char* const argv[] = {"remote-pa", "--client", "--version"};
    const Args args = make_args(3, argv);
    ASSERT_EQ(args.size(), 2U);
    EXPECT_STREQ(args[0], "--client");
}

TEST(MakeArgsTest, ReturnsEmptyForNoArguments) {
    const char* const argv[] = {"remote-pa"};
    EXPECT_TRUE(make_args(1, argv).empty());
    EXPECT_TRUE(make_args(0, nullptr).empty());
}

TEST(HasFlagTest, MatchesWholeArgumentOnly) {
    const char* const argv[] = {"pa-engine", "--version-check"};
    const Args args = make_args(2, argv);
    EXPECT_FALSE(has_flag(args, "--version"));
    EXPECT_TRUE(has_flag(args, "--version-check"));
}

}  // namespace
}  // namespace vocroz::common
