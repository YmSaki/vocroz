// SPDX-License-Identifier: AGPL-3.0-only
//
// SetGain の送り元ごとの順序の判定のテスト。

#include "gain_sequence_gate.h"

#include <gtest/gtest.h>

namespace vocroz::session {
namespace {

TEST(GainSequenceGateTest, DropsAnOlderSeqFromTheSameOrigin) {
    GainSequenceGate gate;
    EXPECT_TRUE(gate.admit("m-a", 5));
    EXPECT_FALSE(gate.admit("m-a", 4));
    EXPECT_TRUE(gate.admit("m-a", 5));
    EXPECT_TRUE(gate.admit("m-a", 6));
}

TEST(GainSequenceGateTest, AcceptsARestartedSeqFromANewOrigin) {
    // 開き直した画面は seq を 1 から数え直すので、前の画面の seq より小さくても受け付ける。
    GainSequenceGate gate;
    EXPECT_TRUE(gate.admit("m-a", 20));
    EXPECT_TRUE(gate.admit("m-b", 1));
    EXPECT_FALSE(gate.admit("m-b", 0));
    EXPECT_TRUE(gate.admit("m-b", 2));
}

TEST(GainSequenceGateTest, ForgetsTheOriginOnReset) {
    GainSequenceGate gate;
    EXPECT_TRUE(gate.admit("m-a", 20));
    gate.reset();
    EXPECT_TRUE(gate.admit("m-a", 1));
}

}  // namespace
}  // namespace vocroz::session
