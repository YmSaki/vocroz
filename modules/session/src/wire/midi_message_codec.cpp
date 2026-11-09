// SPDX-License-Identifier: AGPL-3.0-only
//
// MIDI のメッセージのエンコードとデコードの実装。

#include "wire/midi_message_codec.h"

#include "vocroz/audio_core/midi_message.h"

namespace vocroz::session::wire {
namespace {

// MIDI レーンのメッセージの種別。制御レーンの種別とは別の名前空間だが、レーンを取り違えたときに
// 制御メッセージとして解釈されないよう、制御レーンの子機発（0x0_）と親機発（0x8_）のどちらでもない値にする。
constexpr std::uint8_t kMidiEvents = 0x40;
constexpr std::size_t kTimeBytes = 4;

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

std::uint32_t read_u32(std::span<const std::uint8_t> bytes) {
    std::uint32_t value = 0;
    for (const std::uint8_t byte : bytes.first(kTimeBytes)) {
        value = (value << 8U) | byte;
    }
    return value;
}

}  // namespace

std::vector<std::uint8_t> encode_midi(std::span<const PeerMidiEvent> events) {
    std::vector<std::uint8_t> out;
    out.reserve(1 + events.size() * (kTimeBytes + 3));
    out.push_back(kMidiEvents);
    for (const PeerMidiEvent& event : events) {
        append_u32(out, event.sender_time_us);
        out.insert(out.end(), event.data.begin(), event.data.begin() + event.size);
    }
    return out;
}

common::Result<std::vector<PeerMidiEvent>, PeerMessageError> decode_midi(
    std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || bytes[0] != kMidiEvents) {
        return common::make_unexpected(PeerMessageError::kUnknownType);
    }
    std::vector<PeerMidiEvent> events;
    std::span<const std::uint8_t> rest = bytes.subspan(1);
    while (!rest.empty()) {
        if (rest.size() <= kTimeBytes) {
            return common::make_unexpected(PeerMessageError::kMalformed);
        }
        PeerMidiEvent event;
        event.sender_time_us = read_u32(rest);
        rest = rest.subspan(kTimeBytes);
        const std::size_t size = audio_core::channel_message_size(rest[0]);
        if (size == 0 || size > rest.size() || !audio_core::is_channel_message(rest.first(size))) {
            return common::make_unexpected(PeerMessageError::kMalformed);
        }
        for (std::size_t i = 0; i < size; ++i) {
            event.data[i] = rest[i];
        }
        event.size = static_cast<std::uint8_t>(size);
        events.push_back(event);
        rest = rest.subspan(size);
    }
    if (events.empty()) {
        return common::make_unexpected(PeerMessageError::kMalformed);
    }
    return events;
}

}  // namespace vocroz::session::wire
