// SPDX-License-Identifier: AGPL-3.0-only
//
// sample_offset_in_block()
// の単体テスト。タイムスタンプからブロック内のサンプル位置への変換を検証する。

#include "midi_timing.h"

#include <chrono>

#include <gtest/gtest.h>

#include "vocroz/audio_core/constants.h"

namespace vocroz::audio_core {
namespace {

using std::chrono::microseconds;
using std::chrono::milliseconds;

// コールバックの先頭（時刻 1 秒、サンプル位置 48000）から始まる、128 サンプルのブロック。
MidiBlockClock block_at(std::uint64_t first_sample) {
    MidiBlockClock clock;
    clock.has_anchor = true;
    clock.anchor_time = common::Clock::TimePoint{std::chrono::seconds(1)};
    clock.anchor_sample = kSampleRate;
    clock.first_sample = first_sample;
    clock.frames = kBlockSize;
    return clock;
}

TEST(MidiTimingTest, WithoutScheduleAheadPastEventsGoToBlockStart) {
    const MidiBlockClock clock = block_at(kSampleRate);
    EXPECT_EQ(sample_offset_in_block(clock, clock.anchor_time - milliseconds(1), 0), 0U);
    EXPECT_EQ(sample_offset_in_block(clock, clock.anchor_time, 0), 0U);
}

TEST(MidiTimingTest, ScheduleAheadKeepsRelativeTiming) {
    const MidiBlockClock clock = block_at(kSampleRate);
    // 1 ブロック先に鳴らすと、コールバックの 1ms 前（48 サンプル前）のイベントは 128 - 48 = 80。
    EXPECT_EQ(sample_offset_in_block(clock, clock.anchor_time - milliseconds(1), kBlockSize), 80U);
    // 2ms 前なら 32。2 つのイベントの間隔 1ms（48 サンプル）がそのまま保たれる。
    EXPECT_EQ(sample_offset_in_block(clock, clock.anchor_time - milliseconds(2), kBlockSize), 32U);
}

TEST(MidiTimingTest, EventsForLaterBlocksAreHeld) {
    // コールバックの先頭そのもののイベントを 1 ブロック先に鳴らすと、次のブロックの先頭に来る。
    const MidiBlockClock first = block_at(kSampleRate);
    EXPECT_EQ(sample_offset_in_block(first, first.anchor_time, kBlockSize), std::nullopt);
    const MidiBlockClock second = block_at(kSampleRate + kBlockSize);
    EXPECT_EQ(sample_offset_in_block(second, first.anchor_time, kBlockSize), 0U);
}

TEST(MidiTimingTest, TimestampsAfterCallbackStartAreTreatedAsNow) {
    const MidiBlockClock clock = block_at(kSampleRate);
    EXPECT_EQ(sample_offset_in_block(clock, clock.anchor_time + std::chrono::hours(1), 0), 0U);
}

TEST(MidiTimingTest, VeryOldTimestampsDoNotOverflow) {
    const MidiBlockClock clock = block_at(kSampleRate);
    EXPECT_EQ(sample_offset_in_block(clock, common::Clock::TimePoint{}, kBlockSize), 0U);
    EXPECT_EQ(
        sample_offset_in_block(clock, clock.anchor_time - std::chrono::hours(24 * 365), kBlockSize),
        0U);
}

TEST(MidiTimingTest, WithoutAnchorEverythingGoesToBlockStart) {
    MidiBlockClock clock;
    clock.frames = kBlockSize;
    EXPECT_EQ(sample_offset_in_block(clock, common::Clock::TimePoint{microseconds(5)}, 64), 0U);
}

}  // namespace
}  // namespace vocroz::audio_core
