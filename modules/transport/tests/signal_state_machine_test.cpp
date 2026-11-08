// SPDX-License-Identifier: AGPL-3.0-only
//
// 信号状態の遷移表の単体テスト。表の全セルを期待値と照合する。

#include "state/signal_state_machine.h"

#include <gtest/gtest.h>

namespace vocroz::transport {
namespace {

using E = SignalEvent;
using S = SignalState;

struct Transition {
    S from;
    E event;
    S to;
};

constexpr Transition kExpected[] = {
    {S::kReceiving, E::kFrameAccepted, S::kReceiving},
    {S::kReceiving, E::kSendMuted, S::kPaused},
    {S::kReceiving, E::kSendUnmuted, S::kResuming},
    {S::kReceiving, E::kReceiveTimeout, S::kNoSignal},
    {S::kReceiving, E::kResumeTimeout, S::kReceiving},
    {S::kPaused, E::kFrameAccepted, S::kReceiving},
    {S::kPaused, E::kSendMuted, S::kPaused},
    {S::kPaused, E::kSendUnmuted, S::kResuming},
    {S::kPaused, E::kReceiveTimeout, S::kPaused},
    {S::kPaused, E::kResumeTimeout, S::kPaused},
    {S::kResuming, E::kFrameAccepted, S::kReceiving},
    {S::kResuming, E::kSendMuted, S::kPaused},
    {S::kResuming, E::kSendUnmuted, S::kResuming},
    {S::kResuming, E::kReceiveTimeout, S::kResuming},
    {S::kResuming, E::kResumeTimeout, S::kNoSignal},
    {S::kNoSignal, E::kFrameAccepted, S::kReceiving},
    {S::kNoSignal, E::kSendMuted, S::kPaused},
    {S::kNoSignal, E::kSendUnmuted, S::kResuming},
    {S::kNoSignal, E::kReceiveTimeout, S::kNoSignal},
    {S::kNoSignal, E::kResumeTimeout, S::kNoSignal},
};

TEST(SignalStateMachineTest, MatchesTransitionTable) {
    for (const Transition& t : kExpected) {
        EXPECT_EQ(next_signal_state(t.from, t.event), t.to)
            << canonical_name(t.from) << " --" << static_cast<int>(t.event) << "-->";
    }
}

TEST(SignalStateMachineTest, IsUsableAtCompileTime) {
    static_assert(next_signal_state(S::kPaused, E::kReceiveTimeout) == S::kPaused);
    static_assert(next_signal_state(S::kReceiving, E::kReceiveTimeout) == S::kNoSignal);
}

TEST(SignalStateMachineTest, CanonicalNamesMatchSpecification) {
    EXPECT_EQ(canonical_name(S::kReceiving), "RECEIVING");
    EXPECT_EQ(canonical_name(S::kPaused), "PAUSED");
    EXPECT_EQ(canonical_name(S::kResuming), "RESUMING");
    EXPECT_EQ(canonical_name(S::kNoSignal), "NO_SIGNAL");
}

}  // namespace
}  // namespace vocroz::transport
