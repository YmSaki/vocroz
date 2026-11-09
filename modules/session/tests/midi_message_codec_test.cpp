// SPDX-License-Identifier: AGPL-3.0-only
//
// MIDI のメッセージのエンコードとデコードの単体テスト。往復と、信頼境界での拒否を検証する。

#include "wire/midi_message_codec.h"

#include <vector>

#include <gtest/gtest.h>

namespace vocroz::session::wire {
namespace {

PeerMidiEvent event(std::uint32_t time, std::uint8_t status, std::uint8_t data1, std::uint8_t data2,
                    std::uint8_t size = 3) {
    PeerMidiEvent e;
    e.sender_time_us = time;
    e.data = {status, data1, size == 3 ? data2 : std::uint8_t{0}};
    e.size = size;
    return e;
}

TEST(MidiMessageCodecTest, RoundTripsSeveralEvents) {
    const std::vector<PeerMidiEvent> events{event(0xFFFFFFF0, 0x90, 60, 100),
                                            event(5, 0xC1, 7, 0, 2), event(6, 0x80, 60, 0)};
    const auto bytes = encode_midi(events);
    EXPECT_EQ(bytes.size(), 1U + 7 + 6 + 7);
    const auto decoded = decode_midi(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), events);
}

TEST(MidiMessageCodecTest, RejectsOtherTypesAndEmptyMessages) {
    EXPECT_EQ(decode_midi({}).error(), PeerMessageError::kUnknownType);
    const std::vector<std::uint8_t> control{0x01, 0, 0, 0, 0, 0x90, 60, 100};
    EXPECT_EQ(decode_midi(control).error(), PeerMessageError::kUnknownType);
    const std::vector<std::uint8_t> header_only{0x40};
    EXPECT_EQ(decode_midi(header_only).error(), PeerMessageError::kMalformed);
}

TEST(MidiMessageCodecTest, RejectsWholeMessageIfAnyEventIsInvalid) {
    auto bytes = encode_midi(std::vector<PeerMidiEvent>{event(1, 0x90, 60, 100)});
    // 2 件目: SysEx（チャンネルメッセージではない）。
    for (const int b : {0, 0, 0, 2, 0xF0, 1, 2}) {
        bytes.push_back(static_cast<std::uint8_t>(b));
    }
    EXPECT_EQ(decode_midi(bytes).error(), PeerMessageError::kMalformed);
}

TEST(MidiMessageCodecTest, RejectsTruncatedAndRunningStatus) {
    auto truncated = encode_midi(std::vector<PeerMidiEvent>{event(1, 0x90, 60, 100)});
    truncated.pop_back();
    EXPECT_EQ(decode_midi(truncated).error(), PeerMessageError::kMalformed);
    // ステータスバイトのないデータバイト（ランニングステータス）。
    const std::vector<std::uint8_t> running{0x40, 0, 0, 0, 1, 60, 100};
    EXPECT_EQ(decode_midi(running).error(), PeerMessageError::kMalformed);
    // データバイトの最上位ビットが立っている。
    const std::vector<std::uint8_t> high{0x40, 0, 0, 0, 1, 0x90, 0x80, 100};
    EXPECT_EQ(decode_midi(high).error(), PeerMessageError::kMalformed);
}

}  // namespace
}  // namespace vocroz::session::wire
