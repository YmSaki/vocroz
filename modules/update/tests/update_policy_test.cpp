// SPDX-License-Identifier: AGPL-3.0-only
//
// decide_apply（本番中のブロック）の単体テスト。

#include "vocroz/update/update_policy.h"

#include <gtest/gtest.h>

namespace vocroz::update {
namespace {

const SemanticVersion kOld{0, 1, 0, "alpha.1"};
const SemanticVersion kNew{0, 1, 0, "alpha.2"};

TEST(DecideApplyTest, AppliesNewerVersionWhenIdleAndRequested) {
    EXPECT_EQ(decide_apply({false, true, kOld, kNew}), ApplyDecision::kApply);
}

TEST(DecideApplyTest, BlocksWhileSessionIsActiveEvenIfRequested) {
    EXPECT_EQ(decide_apply({true, true, kOld, kNew}), ApplyDecision::kBlockedBySession);
}

TEST(DecideApplyTest, SessionBlockTakesPriorityOverOtherReasons) {
    EXPECT_EQ(decide_apply({true, false, kNew, kOld}), ApplyDecision::kBlockedBySession);
}

TEST(DecideApplyTest, RequiresExplicitUserRequest) {
    EXPECT_EQ(decide_apply({false, false, kOld, kNew}), ApplyDecision::kNotRequested);
}

TEST(DecideApplyTest, RejectsSameOrOlderVersion) {
    EXPECT_EQ(decide_apply({false, true, kNew, kNew}), ApplyDecision::kNotNewer);
    EXPECT_EQ(decide_apply({false, true, kNew, kOld}), ApplyDecision::kNotNewer);
}

}  // namespace
}  // namespace vocroz::update
