// SPDX-License-Identifier: AGPL-3.0-only
//
// PeerMidiInbox の単体テスト。承認前遮断、途絶での Panic と受け付けの停止、復帰、
// 再接続での同じキューの再利用、接続の終了とストリップの削除を検証する。

#include "vocroz/session/peer_midi_inbox.h"

#include <array>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/audio_core/constants.h"

#include "fakes/fake_clock.h"
#include "wire/midi_message_codec.h"

namespace vocroz::session {
namespace {

constexpr PeerHandle kPeer = 7;
constexpr PeerHandle kNewPeer = 8;
const std::string kStrip = "ch1";

std::vector<std::uint8_t> note_on_message(std::uint8_t note) {
    wire::PeerMidiEvent event;
    event.sender_time_us = 1000;
    event.data = {0x90, note, 100};
    event.size = 3;
    return wire::encode_midi(std::vector<wire::PeerMidiEvent>{event});
}

audio_core::MidiPullResult pull(audio_core::MidiInputQueue& queue) {
    std::array<audio_core::MidiEvent, audio_core::kBlockSize> out{};
    audio_core::MidiBlockClock clock;
    clock.frames = audio_core::kBlockSize;
    return queue.pull(clock, out);
}

TEST(PeerMidiInboxTest, DropsMidiFromPeersNotAdmitted) {
    common::FakeClock clock;
    PeerMidiInbox inbox(clock);
    inbox.on_midi_message(kPeer, note_on_message(60));
    EXPECT_EQ(inbox.rejected_count(), 1U);
    EXPECT_EQ(inbox.queue_for(kStrip), nullptr);
    auto queue = inbox.admit(kPeer, kStrip);
    EXPECT_EQ(pull(*queue).count, 0U);  // 承認前のぶんは残っていない
}

TEST(PeerMidiInboxTest, QueuesMidiFromAdmittedPeer) {
    common::FakeClock clock;
    PeerMidiInbox inbox(clock);
    auto queue = inbox.admit(kPeer, kStrip);
    EXPECT_EQ(inbox.queue_for(kStrip), queue);
    inbox.on_midi_message(kPeer, note_on_message(60));
    EXPECT_EQ(pull(*queue).count, 1U);
}

TEST(PeerMidiInboxTest, MalformedMessagesAreRejected) {
    common::FakeClock clock;
    PeerMidiInbox inbox(clock);
    auto queue = inbox.admit(kPeer, kStrip);
    const std::vector<std::uint8_t> bad{0x40, 0, 0, 0, 1, 0xF0};
    inbox.on_midi_message(kPeer, bad);
    EXPECT_EQ(inbox.rejected_count(), 1U);
    EXPECT_EQ(pull(*queue).count, 0U);
}

TEST(PeerMidiInboxTest, SuspendRequestsPanicAndStopsAcceptingUntilResume) {
    common::FakeClock clock;
    PeerMidiInbox inbox(clock);
    auto queue = inbox.admit(kPeer, kStrip);
    inbox.on_midi_message(kPeer, note_on_message(60));
    inbox.suspend(kPeer);
    inbox.on_midi_message(kPeer, note_on_message(62));
    const auto result = pull(*queue);
    EXPECT_TRUE(result.panic);
    EXPECT_EQ(result.count, 0U);
    inbox.resume(kPeer);
    inbox.on_midi_message(kPeer, note_on_message(64));
    EXPECT_EQ(pull(*queue).count, 1U);
}

TEST(PeerMidiInboxTest, ReconnectReusesTheStripQueueAndDropsTheOldPeer) {
    common::FakeClock clock;
    PeerMidiInbox inbox(clock);
    auto queue = inbox.admit(kPeer, kStrip);
    inbox.on_midi_message(kPeer, note_on_message(60));
    EXPECT_EQ(inbox.admit(kNewPeer, kStrip), queue);
    // 前の接続のノートは Panic で止まり、前の接続からの MIDI は捨てる。
    EXPECT_TRUE(pull(*queue).panic);
    inbox.on_midi_message(kPeer, note_on_message(62));
    EXPECT_EQ(inbox.rejected_count(), 1U);
    inbox.on_midi_message(kNewPeer, note_on_message(64));
    EXPECT_EQ(pull(*queue).count, 1U);
}

TEST(PeerMidiInboxTest, CloseRequestsPanicAndKeepsTheQueue) {
    common::FakeClock clock;
    PeerMidiInbox inbox(clock);
    auto queue = inbox.admit(kPeer, kStrip);
    inbox.close(kPeer);
    inbox.on_midi_message(kPeer, note_on_message(60));
    EXPECT_TRUE(pull(*queue).panic);
    EXPECT_EQ(inbox.rejected_count(), 1U);
    EXPECT_EQ(inbox.queue_for(kStrip), queue);
}

TEST(PeerMidiInboxTest, RemoveStripRequestsPanicAndForgetsTheQueue) {
    common::FakeClock clock;
    PeerMidiInbox inbox(clock);
    auto queue = inbox.admit(kPeer, kStrip);
    inbox.remove_strip(kStrip);
    inbox.on_midi_message(kPeer, note_on_message(60));
    EXPECT_TRUE(pull(*queue).panic);
    EXPECT_EQ(inbox.rejected_count(), 1U);
    EXPECT_EQ(inbox.queue_for(kStrip), nullptr);
}

}  // namespace
}  // namespace vocroz::session
