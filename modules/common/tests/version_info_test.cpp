// SPDX-License-Identifier: AGPL-3.0-only
//
// vocroz::common::version_line の単体テスト。

#include "vocroz/common/version_info.h"

#include <string>

#include <gtest/gtest.h>

#include "vocroz/version.h"

namespace vocroz::common {
namespace {

TEST(VersionLineTest, ContainsProductAppAndVersion) {
    const std::string expected = std::string{kProductName} + " pa-engine " + kVersion;
    EXPECT_EQ(version_line("pa-engine"), expected);
}

}  // namespace
}  // namespace vocroz::common
