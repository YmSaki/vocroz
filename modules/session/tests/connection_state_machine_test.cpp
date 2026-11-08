// SPDX-License-Identifier: AGPL-3.0-only
//
// 参加者の接続状態の遷移表のテスト。

#include "state/connection_state_machine.h"

#include <gtest/gtest.h>

namespace vocroz::session::state {
namespace {

using S = control_protocol::ConnectionState;
using E = ConnectionEvent;

TEST(ConnectionStateMachineTest, ApprovalLeadsToConnectedThroughConnecting) {
    EXPECT_EQ(next_state(S::kPending, E::kApprove), S::kConnecting);
    EXPECT_EQ(next_state(S::kConnecting, E::kEstablished), S::kConnected);
    EXPECT_EQ(next_state(S::kConnecting, E::kPathFailed), S::kFailed);
}

TEST(ConnectionStateMachineTest, TimeoutThenGraceExpiryLeadsToLost) {
    EXPECT_EQ(next_state(S::kConnected, E::kReceiveTimeout), S::kReconnecting);
    EXPECT_EQ(next_state(S::kReconnecting, E::kResumed), S::kConnected);
    EXPECT_EQ(next_state(S::kReconnecting, E::kGraceExpired), S::kLost);
    EXPECT_EQ(next_state(S::kConnected, E::kGraceExpired), std::nullopt);
}

TEST(ConnectionStateMachineTest, ReconnectTokenReturnsFromAnyDisconnectedStateToTheSameStrip) {
    EXPECT_EQ(next_state(S::kReconnecting, E::kReconnectStarted), S::kConnecting);
    EXPECT_EQ(next_state(S::kLost, E::kReconnectStarted), S::kConnecting);
    EXPECT_EQ(next_state(S::kFailed, E::kReconnectStarted), S::kConnecting);
    EXPECT_EQ(next_state(S::kConnected, E::kReconnectStarted), S::kConnecting);
    // 承認前の参加者は再接続トークンを持たない。
    EXPECT_EQ(next_state(S::kPending, E::kReconnectStarted), std::nullopt);
}

TEST(ConnectionStateMachineTest, PeerLeavingKeepsTheStripAsLost) {
    EXPECT_EQ(next_state(S::kConnected, E::kPeerLeft), S::kLost);
    EXPECT_EQ(next_state(S::kReconnecting, E::kPeerLeft), S::kLost);
    EXPECT_EQ(next_state(S::kConnecting, E::kPeerLeft), S::kLost);
    EXPECT_EQ(next_state(S::kLost, E::kPeerLeft), std::nullopt);
}

}  // namespace
}  // namespace vocroz::session::state
