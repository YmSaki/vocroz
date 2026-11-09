// SPDX-License-Identifier: AGPL-3.0-only
//
// activity_for() と IdleShutdownTimer の単体テスト。

#include "engine_activity.h"

#include <chrono>

#include <gtest/gtest.h>

namespace vocroz::engine {
namespace {

using control_protocol::SessionState;
using std::chrono_literals::operator""s;

TEST(EngineActivityTest, PreventsSleepOnlyWhileActive) {
    EXPECT_EQ(activity_for(SessionState::kIdle), (EngineActivity{false, false}));
    EXPECT_EQ(activity_for(SessionState::kReady), (EngineActivity{true, false}));
    EXPECT_EQ(activity_for(SessionState::kActive), (EngineActivity{true, true}));
}

// ERROR と途中の状態も「セッションがある」として、本番中の更新を止める側に倒す。
TEST(EngineActivityTest, TreatsTransitionalAndErrorStatesAsSessionExisting) {
    EXPECT_TRUE(activity_for(SessionState::kCreating).session_exists);
    EXPECT_TRUE(activity_for(SessionState::kEnding).session_exists);
    EXPECT_TRUE(activity_for(SessionState::kError).session_exists);
}

TEST(IdleShutdownTimerTest, ExitsAfterTimeoutWithoutSessionAndUi) {
    IdleShutdownTimer timer(30s);
    const common::Clock::TimePoint start{};
    EXPECT_FALSE(timer.should_exit(start, false, false));
    EXPECT_FALSE(timer.should_exit(start + 29s, false, false));
    EXPECT_TRUE(timer.should_exit(start + 30s, false, false));
}

// UI が落ちても、セッションがある間は終わらない（UI の障害で音を止めない）。
TEST(IdleShutdownTimerTest, KeepsRunningWhileSessionExists) {
    IdleShutdownTimer timer(30s);
    const common::Clock::TimePoint start{};
    EXPECT_FALSE(timer.should_exit(start, false, true));
    EXPECT_FALSE(timer.should_exit(start + 3600s, false, true));
}

// UI がつながると数え直す。
TEST(IdleShutdownTimerTest, RestartsCountingWhenUiConnects) {
    IdleShutdownTimer timer(30s);
    const common::Clock::TimePoint start{};
    EXPECT_FALSE(timer.should_exit(start, false, false));
    EXPECT_FALSE(timer.should_exit(start + 20s, true, false));
    EXPECT_FALSE(timer.should_exit(start + 25s, false, false));
    EXPECT_FALSE(timer.should_exit(start + 54s, false, false));
    EXPECT_TRUE(timer.should_exit(start + 55s, false, false));
}

}  // namespace
}  // namespace vocroz::engine
