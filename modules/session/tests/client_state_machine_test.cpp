// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機の接続状態の遷移表のテスト。

#include "state/client_state_machine.h"

#include <gtest/gtest.h>

namespace vocroz::session::state {
namespace {

using S = control_protocol::ClientState;
using E = ClientEvent;

TEST(ClientStateMachineTest, JoinProceedsThroughApprovalToConnected) {
    EXPECT_EQ(next_state(S::kDisconnected, E::kJoin), S::kResolving);
    EXPECT_EQ(next_state(S::kResolving, E::kResolved), S::kAuthenticating);
    EXPECT_EQ(next_state(S::kAuthenticating, E::kJoinPending), S::kWaitingApproval);
    EXPECT_EQ(next_state(S::kWaitingApproval, E::kApproved), S::kConnecting);
    EXPECT_EQ(next_state(S::kConnecting, E::kMediaEstablished), S::kConnected);
}

TEST(ClientStateMachineTest, ReconnectTokenSkipsTheApprovalWait) {
    EXPECT_EQ(next_state(S::kAuthenticating, E::kApproved), S::kConnecting);
}

TEST(ClientStateMachineTest, LinkLossAfterApprovalReconnects) {
    EXPECT_EQ(next_state(S::kConnected, E::kLinkLost), S::kReconnecting);
    EXPECT_EQ(next_state(S::kConnecting, E::kLinkLost), S::kReconnecting);
    EXPECT_EQ(next_state(S::kReconnecting, E::kReconnected), S::kConnected);
    // 承認前は再接続トークンがないので、途絶は失敗として扱う。
    EXPECT_EQ(next_state(S::kWaitingApproval, E::kLinkLost), std::nullopt);
}

TEST(ClientStateMachineTest, FailedIsTerminalUntilTheUserJoinsAgain) {
    EXPECT_EQ(next_state(S::kReconnecting, E::kFailed), S::kFailed);
    EXPECT_EQ(next_state(S::kFailed, E::kReconnected), std::nullopt);
    EXPECT_EQ(next_state(S::kFailed, E::kJoin), S::kResolving);
    EXPECT_EQ(next_state(S::kConnected, E::kJoin), std::nullopt);
}

TEST(ClientStateMachineTest, LeaveAndSessionEndReturnToDisconnected) {
    for (const auto s : {S::kResolving, S::kAuthenticating, S::kWaitingApproval, S::kConnecting,
                         S::kConnected, S::kReconnecting, S::kFailed, S::kDisconnected}) {
        EXPECT_EQ(next_state(s, E::kLeave), S::kDisconnected);
    }
    EXPECT_EQ(next_state(S::kConnected, E::kSessionEnded), S::kDisconnected);
    EXPECT_EQ(next_state(S::kResolving, E::kSessionEnded), std::nullopt);
}

}  // namespace
}  // namespace vocroz::session::state
