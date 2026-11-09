// SPDX-License-Identifier: AGPL-3.0-only
//
// midi_input_utils の単体テスト。ライブラリの時刻から Clock の時刻への写し方と、
// チャンネルメッセージだけを通すことを検証する。

#include "detail/midi_input_utils.h"

#include <array>
#include <chrono>

#include <gtest/gtest.h>

namespace vocroz::device::detail {
namespace {

using namespace std::chrono_literals;

const common::Clock::TimePoint kNow{std::chrono::seconds{100}};

TEST(MidiInputUtilsTest, MapsLibraryAgeOntoClock) {
    EXPECT_EQ(to_clock_time(kNow, 5000.0, 4997.5), kNow - 2500us);
}

TEST(MidiInputUtilsTest, FutureTimestampsBecomeNow) {
    EXPECT_EQ(to_clock_time(kNow, 5000.0, 5010.0), kNow);
}

TEST(MidiInputUtilsTest, VeryOldTimestampsAreClampedToOneSecond) {
    EXPECT_EQ(to_clock_time(kNow, 5000.0, 0.0), kNow - 1s);
}

TEST(MidiInputUtilsTest, ConvertsChannelMessages) {
    const std::array<std::uint8_t, 3> note_on{0x91, 60, 100};
    const auto message = to_timestamped_message(note_on, kNow);
    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(message->timestamp, kNow);
    EXPECT_EQ(message->size, 3U);
    EXPECT_EQ(message->data, note_on);

    const std::array<std::uint8_t, 2> program_change{0xC0, 5};
    const auto two_bytes = to_timestamped_message(program_change, kNow);
    ASSERT_TRUE(two_bytes.has_value());
    EXPECT_EQ(two_bytes->size, 2U);
}

TEST(MidiInputUtilsTest, RejectsSysExSystemAndMalformedMessages) {
    const std::array<std::uint8_t, 3> sysex{0xF0, 0x7E, 0xF7};
    const std::array<std::uint8_t, 1> clock{0xF8};
    const std::array<std::uint8_t, 2> short_note_on{0x90, 60};
    const std::array<std::uint8_t, 3> running_status{60, 100, 0};
    EXPECT_FALSE(to_timestamped_message(sysex, kNow).has_value());
    EXPECT_FALSE(to_timestamped_message(clock, kNow).has_value());
    EXPECT_FALSE(to_timestamped_message(short_note_on, kNow).has_value());
    EXPECT_FALSE(to_timestamped_message(running_status, kNow).has_value());
}

}  // namespace
}  // namespace vocroz::device::detail
