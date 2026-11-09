// SPDX-License-Identifier: AGPL-3.0-only
//
// midi_message.h の単体テスト。チャンネルメッセージの検証と、Note On / Off の判定を検証する。

#include "vocroz/audio_core/midi_message.h"

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

namespace vocroz::audio_core {
namespace {

MidiEvent event_of(std::uint8_t status, std::uint8_t data1, std::uint8_t data2) {
    const std::array<std::uint8_t, 3> bytes{status, data1, data2};
    return make_midi_event(bytes);
}

TEST(MidiMessageTest, AcceptsOnlyCompleteChannelMessages) {
    EXPECT_TRUE(is_channel_message(std::array<std::uint8_t, 3>{0x90, 60, 100}));
    EXPECT_TRUE(is_channel_message(std::array<std::uint8_t, 2>{0xC3, 5}));
    // 長さがステータスと合わない、データバイトの最上位ビットが立っている、システムメッセージ。
    EXPECT_FALSE(is_channel_message(std::array<std::uint8_t, 2>{0x90, 60}));
    EXPECT_FALSE(is_channel_message(std::array<std::uint8_t, 3>{0xC3, 5, 0}));
    EXPECT_FALSE(is_channel_message(std::array<std::uint8_t, 3>{0x90, 0x80, 100}));
    EXPECT_FALSE(is_channel_message(std::array<std::uint8_t, 1>{0xF8}));
    EXPECT_FALSE(is_channel_message(std::array<std::uint8_t, 2>{60, 100}));
    EXPECT_FALSE(is_channel_message({}));
}

TEST(MidiMessageTest, InvalidBytesMakeEmptyEvent) {
    const std::array<std::uint8_t, 3> sysex{0xF0, 1, 2};
    EXPECT_EQ(make_midi_event(sysex).size, 0);
}

TEST(MidiMessageTest, NoteOnWithZeroVelocityIsNoteOff) {
    EXPECT_TRUE(is_note_on(event_of(0x90, 60, 1)));
    EXPECT_FALSE(is_note_on(event_of(0x90, 60, 0)));
    EXPECT_TRUE(is_note_off(event_of(0x90, 60, 0)));
    EXPECT_TRUE(is_note_off(event_of(0x85, 60, 30)));
    EXPECT_EQ(channel_of(event_of(0x85, 60, 30)), 5);
}

TEST(MidiMessageTest, ReleaseMessagesStopSoundingNotes) {
    EXPECT_TRUE(is_release_message(make_note_off(0, 60)));
    EXPECT_TRUE(is_release_message(make_control_change(0, kMidiSustainController, 0)));
    EXPECT_FALSE(is_release_message(make_control_change(0, kMidiSustainController, 127)));
    EXPECT_TRUE(is_release_message(make_control_change(2, kMidiAllNotesOffController, 0)));
    EXPECT_TRUE(is_release_message(make_control_change(2, kMidiAllSoundOffController, 0)));
    EXPECT_FALSE(is_release_message(event_of(0x90, 60, 100)));
    EXPECT_FALSE(is_release_message(make_control_change(0, 7, 0)));
}

TEST(MidiMessageTest, SustainPedalUsesSwitchThreshold) {
    EXPECT_EQ(sustain_pedal_of(make_control_change(0, kMidiSustainController, 64)), true);
    EXPECT_EQ(sustain_pedal_of(make_control_change(0, kMidiSustainController, 63)), false);
    EXPECT_EQ(sustain_pedal_of(make_control_change(0, 7, 100)), std::nullopt);
}

TEST(MidiMessageTest, BuildersMaskOutOfRangeValues) {
    const MidiEvent off = make_note_off(17, 200, 5);
    EXPECT_EQ(off.data[0], 0x81);
    EXPECT_EQ(off.data[1], 200 & 0x7F);
    EXPECT_EQ(off.sample_offset, 5U);
    EXPECT_TRUE(is_channel_message(std::span<const std::uint8_t>(off.data.data(), off.size)));
}

}  // namespace
}  // namespace vocroz::audio_core
