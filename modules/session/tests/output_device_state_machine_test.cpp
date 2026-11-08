// SPDX-License-Identifier: AGPL-3.0-only
//
// 出力デバイスの状態の遷移表のテスト。

#include "state/output_device_state_machine.h"

#include <gtest/gtest.h>

namespace vocroz::session::state {
namespace {

using S = control_protocol::OutputDeviceState;
using E = OutputDeviceEvent;

TEST(OutputDeviceStateMachineTest, LosingTheOpenDeviceStaysLostUntilAnotherIsOpened) {
    EXPECT_EQ(next_state(S::kNone, E::kOpened), S::kOpen);
    EXPECT_EQ(next_state(S::kOpen, E::kLost), S::kLost);
    EXPECT_EQ(next_state(S::kLost, E::kOpened), S::kOpen);
    EXPECT_EQ(next_state(S::kLost, E::kClosed), S::kNone);
}

TEST(OutputDeviceStateMachineTest, IgnoresLossWhenNoDeviceIsInUse) {
    EXPECT_EQ(next_state(S::kNone, E::kLost), std::nullopt);
    EXPECT_EQ(next_state(S::kLost, E::kLost), std::nullopt);
    EXPECT_EQ(next_state(S::kNone, E::kClosed), std::nullopt);
}

}  // namespace
}  // namespace vocroz::session::state
