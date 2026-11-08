// SPDX-License-Identifier: AGPL-3.0-only
//
// 更新トランザクションの遷移表（ロールバックの判定）の単体テスト。

#include "state/update_state_machine.h"

#include <gtest/gtest.h>

namespace vocroz::update::state {
namespace {

using E = UpdateEvent;
using S = UpdateState;

TEST(UpdateStateMachineTest, SucceedingStepsReachCommitted) {
    S state = next_state(S::kIdle, E::kStart);
    const S expected[] = {S::kBackingUp,      S::kExtracting, S::kVerifyingSignature,
                          S::kCheckingHealth, S::kSwitching,  S::kCommitted};
    for (const S next : expected) {
        state = next_state(state, E::kStepSucceeded);
        EXPECT_EQ(state, next);
    }
    EXPECT_TRUE(is_terminal(state));
}

TEST(UpdateStateMachineTest, FailuresBeforeTouchingInstallDirAbortWithoutRollback) {
    EXPECT_EQ(next_state(S::kVerifyingPackage, E::kStepFailed), S::kAborted);
    EXPECT_EQ(next_state(S::kBackingUp, E::kStepFailed), S::kAborted);
}

TEST(UpdateStateMachineTest, FailuresAfterTouchingInstallDirRollBack) {
    for (const S state :
         {S::kExtracting, S::kVerifyingSignature, S::kCheckingHealth, S::kSwitching}) {
        EXPECT_EQ(next_state(state, E::kStepFailed), S::kRollingBack);
    }
    EXPECT_EQ(next_state(S::kRollingBack, E::kRollbackFinished), S::kRolledBack);
    EXPECT_TRUE(is_terminal(S::kRolledBack));
}

TEST(UpdateStateMachineTest, IgnoresEventsThatDoNotApply) {
    EXPECT_EQ(next_state(S::kIdle, E::kStepSucceeded), S::kIdle);
    EXPECT_EQ(next_state(S::kRollingBack, E::kStepSucceeded), S::kRollingBack);
    EXPECT_EQ(next_state(S::kCommitted, E::kStepFailed), S::kCommitted);
    EXPECT_EQ(next_state(S::kAborted, E::kStart), S::kAborted);
}

}  // namespace
}  // namespace vocroz::update::state
