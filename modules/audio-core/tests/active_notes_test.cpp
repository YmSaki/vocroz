// SPDX-License-Identifier: AGPL-3.0-only
//
// ActiveNotes の単体テスト。発音中のノートの追跡と、Panic で出す Note Off を検証する。

#include "active_notes.h"

#include <array>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::audio_core {
namespace {

MidiEvent note_on(std::uint8_t channel, std::uint8_t note) {
    const std::array<std::uint8_t, 3> bytes{static_cast<std::uint8_t>(0x90 | channel), note, 100};
    return make_midi_event(bytes);
}

std::vector<MidiEvent> release(ActiveNotes& notes, std::uint32_t offset = 0) {
    std::vector<MidiEvent> out(kMaxReleaseEvents);
    out.resize(notes.release_all(out, offset));
    return out;
}

TEST(ActiveNotesTest, ReleasesOnlyNotesStillSounding) {
    ActiveNotes notes;
    notes.observe(note_on(0, 60));
    notes.observe(note_on(0, 64));
    notes.observe(make_note_off(0, 60));
    notes.observe(note_on(9, 36));
    const auto released = release(notes, 7);
    ASSERT_EQ(released.size(), 2U);
    EXPECT_EQ(released[0].data[0], 0x80);
    EXPECT_EQ(released[0].data[1], 64);
    EXPECT_EQ(released[1].data[0], 0x89);
    EXPECT_EQ(released[1].data[1], 36);
    EXPECT_EQ(released[1].sample_offset, 7U);
    EXPECT_FALSE(notes.any());
    EXPECT_TRUE(release(notes).empty());
}

TEST(ActiveNotesTest, ReleasesSustainAfterNotes) {
    ActiveNotes notes;
    notes.observe(make_control_change(3, kMidiSustainController, 127));
    notes.observe(note_on(3, 50));
    notes.observe(make_note_off(3, 50));  // ペダルで鳴り続けている
    EXPECT_TRUE(notes.any());
    const auto released = release(notes);
    ASSERT_EQ(released.size(), 1U);
    EXPECT_EQ(sustain_pedal_of(released[0]), false);
    EXPECT_EQ(channel_of(released[0]), 3);
}

TEST(ActiveNotesTest, AllNotesOffClearsChannelButKeepsPedal) {
    ActiveNotes notes;
    notes.observe(note_on(1, 60));
    notes.observe(note_on(2, 60));
    notes.observe(make_control_change(1, kMidiAllNotesOffController, 0));
    const auto released = release(notes);
    ASSERT_EQ(released.size(), 1U);
    EXPECT_EQ(channel_of(released[0]), 2);
}

TEST(ActiveNotesTest, EveryNoteOnEveryChannelFitsInReleaseCapacity) {
    ActiveNotes notes;
    for (std::uint8_t ch = 0; ch < kMidiChannels; ++ch) {
        notes.observe(make_control_change(ch, kMidiSustainController, 127));
        for (std::uint8_t note = 0; note < kMidiNotes; ++note) {
            notes.observe(note_on(ch, note));
        }
    }
    EXPECT_EQ(release(notes).size(), kMaxReleaseEvents);
}

TEST(ActiveNotesTest, KeepsNotesThatDidNotFitForNextRelease) {
    ActiveNotes notes;
    notes.observe(note_on(0, 1));
    notes.observe(note_on(0, 2));
    std::array<MidiEvent, 1> small{};
    EXPECT_EQ(notes.release_all(small, 0), 1U);
    EXPECT_TRUE(notes.any());
    EXPECT_EQ(release(notes).size(), 1U);
}

TEST(ActiveNotesTest, MergeUnitesBothSets) {
    ActiveNotes a;
    ActiveNotes b;
    a.observe(note_on(0, 60));
    b.observe(note_on(0, 61));
    a.merge(b);
    EXPECT_EQ(release(a).size(), 2U);
}

}  // namespace
}  // namespace vocroz::audio_core
