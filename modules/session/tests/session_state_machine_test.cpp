// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機のセッション状態の遷移表のテスト。

#include "state/session_state_machine.h"

#include <gtest/gtest.h>

namespace vocroz::session::state {
namespace {

using S = control_protocol::SessionState;
using E = SessionEvent;

TEST(SessionStateMachineTest, FollowsTheLifecycleFromIdleBackToIdle) {
    EXPECT_EQ(next_state(S::kIdle, E::kCreate), S::kCreating);
    EXPECT_EQ(next_state(S::kCreating, E::kCreated), S::kReady);
    EXPECT_EQ(next_state(S::kReady, E::kParticipantActive), S::kActive);
    EXPECT_EQ(next_state(S::kActive, E::kNoParticipantActive), S::kReady);
    EXPECT_EQ(next_state(S::kActive, E::kEnd), S::kEnding);
    EXPECT_EQ(next_state(S::kReady, E::kEnd), S::kEnding);
    EXPECT_EQ(next_state(S::kEnding, E::kEnded), S::kIdle);
}

TEST(SessionStateMachineTest, EntersErrorOnlyFromCreatingReadyOrActive) {
    EXPECT_EQ(next_state(S::kCreating, E::kFault), S::kError);
    EXPECT_EQ(next_state(S::kReady, E::kFault), S::kError);
    EXPECT_EQ(next_state(S::kActive, E::kFault), S::kError);
    EXPECT_EQ(next_state(S::kIdle, E::kFault), std::nullopt);
    EXPECT_EQ(next_state(S::kEnding, E::kFault), std::nullopt);
    EXPECT_EQ(next_state(S::kError, E::kRecover), S::kIdle);
}

TEST(SessionStateMachineTest, RejectsCreatingTwiceAndEndingWhileCreating) {
    EXPECT_EQ(next_state(S::kReady, E::kCreate), std::nullopt);
    EXPECT_EQ(next_state(S::kCreating, E::kEnd), std::nullopt);
    EXPECT_EQ(next_state(S::kIdle, E::kEnd), std::nullopt);
    EXPECT_EQ(next_state(S::kError, E::kEnd), std::nullopt);
}

}  // namespace
}  // namespace vocroz::session::state
