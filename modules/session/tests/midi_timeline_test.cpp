// SPDX-License-Identifier: AGPL-3.0-only
//
// MidiTimeline
// の単体テスト。子機の時刻を最小の片道遅延で写すこと、桁あふれ、窓の取り直しを検証する。

#include "midi_timeline.h"

#include <chrono>

#include <gtest/gtest.h>

#include "session_constants.h"

namespace vocroz::session {
namespace {

using std::chrono::milliseconds;
using TimePoint = common::Clock::TimePoint;

constexpr TimePoint kStart{std::chrono::seconds(100)};

std::uint32_t us(milliseconds ms) {
    return static_cast<std::uint32_t>(std::chrono::microseconds(ms).count());
}

TEST(MidiTimelineTest, RestoresIntervalsSquashedByJitter) {
    MidiTimeline timeline;
    // 子機で 0ms と 10ms に打鍵。1 件目は 20ms、2 件目は揺れで 5ms 遅い 35ms で届いた。
    const TimePoint first = timeline.map(us(milliseconds(0)), kStart + milliseconds(20));
    const TimePoint second = timeline.map(us(milliseconds(10)), kStart + milliseconds(35));
    EXPECT_EQ(first, kStart + milliseconds(20));
    // 最小の遅延（20ms）で写すので、間隔は子機での 10ms に戻る。
    EXPECT_EQ(second - first, milliseconds(10));
}

TEST(MidiTimelineTest, NeverMapsAfterArrival) {
    MidiTimeline timeline;
    (void)timeline.map(us(milliseconds(0)), kStart + milliseconds(30));
    // 前より速く届いた（遅延 20ms）。写した時刻は受信時刻と同じで、後にならない。
    const TimePoint arrival = kStart + milliseconds(30);
    EXPECT_EQ(timeline.map(us(milliseconds(10)), arrival), arrival);
}

TEST(MidiTimelineTest, HandlesSenderClockWrap) {
    MidiTimeline timeline;
    const std::uint32_t before_wrap = 0xFFFFFFFFU - 999U;  // 一周の 1ms 前
    const TimePoint first = timeline.map(before_wrap, kStart);
    const TimePoint second = timeline.map(1000U, kStart + milliseconds(2));
    EXPECT_EQ(second - first, milliseconds(2));
}

TEST(MidiTimelineTest, ForgetsOldMinimumAfterTwoWindows) {
    MidiTimeline timeline;
    // 最初は遅延 10ms。経路が変わって遅延が 40ms に増えた。
    (void)timeline.map(0, kStart + milliseconds(10));
    TimePoint now = kStart;
    std::uint32_t sender = 0;
    for (int i = 0; i < 3; ++i) {
        now += kMidiTimelineWindow;
        sender +=
            static_cast<std::uint32_t>(std::chrono::microseconds(kMidiTimelineWindow).count());
        (void)timeline.map(sender, now + milliseconds(40));
    }
    // 古い最小値（10ms）が窓から外れ、40ms の遅延で写す（受信時刻と同じになる）。
    EXPECT_EQ(timeline.map(sender, now + milliseconds(40)), now + milliseconds(40));
}

}  // namespace
}  // namespace vocroz::session
