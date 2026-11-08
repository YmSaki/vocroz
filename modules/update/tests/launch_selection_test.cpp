// SPDX-License-Identifier: AGPL-3.0-only
//
// 起動スタブが試すバージョンの順序の単体テスト。

#include "vocroz/update/launch_selection.h"

#include <gtest/gtest.h>

namespace vocroz::update {
namespace {

using Versions = std::vector<SemanticVersion>;

const SemanticVersion kOld{0, 1, 0, ""};
const SemanticVersion kMid{0, 2, 0, ""};
const SemanticVersion kNew{0, 3, 0, ""};

TEST(LaunchCandidatesTest, UsesOwnVersionWithoutPointer) {
    EXPECT_EQ(launch_candidates(std::nullopt, kOld), (Versions{kOld}));
}

TEST(LaunchCandidatesTest, PrefersUpdatedVersionThenFallsBack) {
    // pa-updater が更新した状態: ポインターの現行版がスタブより新しい。
    EXPECT_EQ(launch_candidates(InstallPointer{kNew, kMid}, kOld), (Versions{kNew, kOld, kMid}));
}

TEST(LaunchCandidatesTest, PrefersReinstalledVersionOverStalePointer) {
    // インストーラーで新しい版を上書き導入した状態: スタブの版が新しい。
    EXPECT_EQ(launch_candidates(InstallPointer{kMid, kOld}, kNew), (Versions{kNew, kMid, kOld}));
}

TEST(LaunchCandidatesTest, RemovesDuplicates) {
    EXPECT_EQ(launch_candidates(InstallPointer{kMid, kOld}, kMid), (Versions{kMid, kOld}));
}

}  // namespace
}  // namespace vocroz::update
