// SPDX-License-Identifier: AGPL-3.0-only
//
// MidiInputQueue の単体テスト。届いた順の保証、サンプル位置への変換、持ち越し、Panic、
// あふれたときの扱いを検証する。

#include "vocroz/audio_core/midi_input_queue.h"

#include <array>
#include <chrono>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/audio_core/constants.h"
#include "vocroz/audio_core/midi_message.h"

namespace vocroz::audio_core {
namespace {

using std::chrono::milliseconds;
using TimePoint = common::Clock::TimePoint;

constexpr TimePoint kCallbackStart{std::chrono::seconds(10)};

TimestampedMidiMessage message(TimePoint timestamp, std::uint8_t status, std::uint8_t data1,
                               std::uint8_t data2) {
    TimestampedMidiMessage m;
    m.timestamp = timestamp;
    m.data = {status, data1, data2};
    m.size = 3;
    return m;
}

MidiBlockClock block(std::uint64_t first_sample) {
    MidiBlockClock clock;
    clock.has_anchor = true;
    clock.anchor_time = kCallbackStart;
    clock.anchor_sample = 0;
    clock.first_sample = first_sample;
    clock.frames = kBlockSize;
    return clock;
}

struct Pulled {
    MidiPullResult result;
    std::vector<MidiEvent> events;
};

Pulled pull(MidiInputQueue& queue, const MidiBlockClock& clock) {
    Pulled pulled;
    pulled.events.resize(kBlockSize);
    pulled.result = queue.pull(clock, pulled.events);
    pulled.events.resize(pulled.result.count);
    return pulled;
}

TEST(MidiInputQueueTest, ConvertsTimestampsToOffsetsWithinBlock) {
    MidiInputQueue queue({kBlockSize});
    ASSERT_TRUE(queue.push(message(kCallbackStart - milliseconds(2), 0x90, 60, 100)));
    ASSERT_TRUE(queue.push(message(kCallbackStart - milliseconds(1), 0x80, 60, 0)));
    const Pulled pulled = pull(queue, block(0));
    ASSERT_EQ(pulled.events.size(), 2U);
    EXPECT_EQ(pulled.events[0].sample_offset, 32U);
    EXPECT_EQ(pulled.events[1].sample_offset, 80U);
    EXPECT_TRUE(is_note_on(pulled.events[0]));
    EXPECT_TRUE(is_note_off(pulled.events[1]));
}

TEST(MidiInputQueueTest, KeepsArrivalOrderEvenIfTimestampsGoBackwards) {
    MidiInputQueue queue({kBlockSize});
    // Note Off のタイムスタンプが Note On より前でも、届いた順（On → Off）に送る。
    ASSERT_TRUE(queue.push(message(kCallbackStart - milliseconds(1), 0x90, 60, 100)));
    ASSERT_TRUE(queue.push(message(kCallbackStart - milliseconds(2), 0x80, 60, 0)));
    const Pulled pulled = pull(queue, block(0));
    ASSERT_EQ(pulled.events.size(), 2U);
    EXPECT_TRUE(is_note_on(pulled.events[0]));
    EXPECT_TRUE(is_note_off(pulled.events[1]));
    EXPECT_LE(pulled.events[0].sample_offset, pulled.events[1].sample_offset);
}

TEST(MidiInputQueueTest, HoldsEventsForLaterBlocksWithoutReordering) {
    MidiInputQueue queue({kBlockSize});
    ASSERT_TRUE(queue.push(message(kCallbackStart, 0x90, 60, 100)));  // 次のブロックの先頭
    ASSERT_TRUE(queue.push(message(kCallbackStart - milliseconds(1), 0x90, 62, 100)));
    EXPECT_TRUE(pull(queue, block(0)).events.empty());
    const Pulled next = pull(queue, block(kBlockSize));
    ASSERT_EQ(next.events.size(), 2U);
    EXPECT_EQ(next.events[0].data[1], 60);
    EXPECT_EQ(next.events[1].data[1], 62);
}

TEST(MidiInputQueueTest, RejectsNonChannelMessages) {
    MidiInputQueue queue;
    TimestampedMidiMessage clock_tick;
    clock_tick.data = {0xF8, 0, 0};
    clock_tick.size = 1;
    EXPECT_FALSE(queue.push(clock_tick));
    EXPECT_FALSE(queue.push(message(kCallbackStart, 0x90, 0x80, 1)));
    EXPECT_EQ(queue.dropped_count(), 0U);
}

TEST(MidiInputQueueTest, PanicDiscardsPendingMessages) {
    MidiInputQueue queue;
    ASSERT_TRUE(queue.push(message(kCallbackStart, 0x90, 60, 100)));
    queue.request_panic();
    const Pulled pulled = pull(queue, block(0));
    EXPECT_TRUE(pulled.result.panic);
    EXPECT_TRUE(pulled.events.empty());
    EXPECT_FALSE(pull(queue, block(kBlockSize)).result.panic);
}

TEST(MidiInputQueueTest, DroppingReleaseMessageOnOverflowRequestsPanic) {
    MidiInputQueue queue;
    for (std::size_t i = 0; i < kMidiQueueCapacity; ++i) {
        ASSERT_TRUE(queue.push(message(kCallbackStart, 0x90, 60, 100)));
    }
    // Note On を落としても音は残らないので Panic にしない。
    EXPECT_FALSE(queue.push(message(kCallbackStart, 0x90, 61, 100)));
    EXPECT_EQ(queue.dropped_count(), 1U);
    MidiInputQueue full_with_release;
    for (std::size_t i = 0; i < kMidiQueueCapacity; ++i) {
        ASSERT_TRUE(full_with_release.push(message(kCallbackStart, 0x90, 60, 100)));
    }
    EXPECT_FALSE(full_with_release.push(message(kCallbackStart, 0x80, 60, 0)));
    EXPECT_TRUE(pull(full_with_release, block(0)).result.panic);
}

TEST(MidiInputQueueTest, EventsBeyondBlockCapacityWaitForNextBlock) {
    MidiInputQueue queue;
    for (std::uint8_t note = 0; note < 10; ++note) {
        ASSERT_TRUE(queue.push(message(kCallbackStart, 0x90, note, 100)));
    }
    std::array<MidiEvent, 4> small{};
    EXPECT_EQ(queue.pull(block(0), small).count, 4U);
    EXPECT_EQ(small[3].data[1], 3);
    EXPECT_EQ(queue.pull(block(kBlockSize), small).count, 4U);
    EXPECT_EQ(small[0].data[1], 4);
}

}  // namespace
}  // namespace vocroz::audio_core
