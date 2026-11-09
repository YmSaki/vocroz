// SPDX-License-Identifier: AGPL-3.0-only
//
// MIDI のチャンネルメッセージの判定と組み立ての実装。

#include "vocroz/audio_core/midi_message.h"

namespace vocroz::audio_core {
namespace {

// ステータスバイトの上位 4 ビット（メッセージの種類）。
constexpr std::uint8_t kNoteOff = 0x80;
constexpr std::uint8_t kNoteOn = 0x90;
constexpr std::uint8_t kControlChange = 0xB0;
constexpr std::uint8_t kProgramChange = 0xC0;
constexpr std::uint8_t kChannelPressure = 0xD0;
// チャンネルメッセージの範囲の終わり（0xF0 以降はシステムメッセージ）。
constexpr std::uint8_t kSystemMessage = 0xF0;
// サステインペダルは 64 以上がオン（MIDI 1.0 のスイッチ型コントローラーの規約）。
constexpr std::uint8_t kSwitchOnThreshold = 64;
// リリースベロシティを持たない操作で使う Note Off のベロシティ（中央値）。
constexpr std::uint8_t kDefaultReleaseVelocity = 64;

constexpr std::uint8_t kind_of(std::uint8_t status) noexcept {
    return static_cast<std::uint8_t>(status & 0xF0U);
}

}  // namespace

std::size_t channel_message_size(std::uint8_t status) noexcept VOCROZ_NONBLOCKING {
    if (status < kNoteOff || status >= kSystemMessage) {
        return 0;
    }
    const std::uint8_t kind = kind_of(status);
    return kind == kProgramChange || kind == kChannelPressure ? 2 : 3;
}

bool is_channel_message(std::span<const std::uint8_t> bytes) noexcept VOCROZ_NONBLOCKING {
    if (bytes.empty() || channel_message_size(bytes[0]) != bytes.size()) {
        return false;
    }
    for (std::size_t i = 1; i < bytes.size(); ++i) {
        if (bytes[i] >= 0x80U) {
            return false;
        }
    }
    return true;
}

MidiEvent make_midi_event(std::span<const std::uint8_t> bytes,
                          std::uint32_t sample_offset) noexcept VOCROZ_NONBLOCKING {
    MidiEvent event;
    event.sample_offset = sample_offset;
    if (!is_channel_message(bytes)) {
        return event;
    }
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        event.data[i] = bytes[i];
    }
    event.size = static_cast<std::uint8_t>(bytes.size());
    return event;
}

MidiEvent make_note_off(std::uint8_t channel, std::uint8_t note,
                        std::uint32_t sample_offset) noexcept VOCROZ_NONBLOCKING {
    MidiEvent event;
    event.sample_offset = sample_offset;
    event.data = {static_cast<std::uint8_t>(kNoteOff | (channel & 0x0FU)),
                  static_cast<std::uint8_t>(note & 0x7FU), kDefaultReleaseVelocity};
    event.size = 3;
    return event;
}

MidiEvent make_control_change(std::uint8_t channel, std::uint8_t controller, std::uint8_t value,
                              std::uint32_t sample_offset) noexcept VOCROZ_NONBLOCKING {
    MidiEvent event;
    event.sample_offset = sample_offset;
    event.data = {static_cast<std::uint8_t>(kControlChange | (channel & 0x0FU)),
                  static_cast<std::uint8_t>(controller & 0x7FU),
                  static_cast<std::uint8_t>(value & 0x7FU)};
    event.size = 3;
    return event;
}

bool is_note_on(const MidiEvent& event) noexcept VOCROZ_NONBLOCKING {
    return event.size == 3 && kind_of(event.data[0]) == kNoteOn && event.data[2] > 0;
}

bool is_note_off(const MidiEvent& event) noexcept VOCROZ_NONBLOCKING {
    if (event.size != 3) {
        return false;
    }
    const std::uint8_t kind = kind_of(event.data[0]);
    return kind == kNoteOff || (kind == kNoteOn && event.data[2] == 0);
}

bool is_control_change(const MidiEvent& event) noexcept VOCROZ_NONBLOCKING {
    return event.size == 3 && kind_of(event.data[0]) == kControlChange;
}

bool is_release_message(const MidiEvent& event) noexcept VOCROZ_NONBLOCKING {
    if (is_note_off(event)) {
        return true;
    }
    if (!is_control_change(event)) {
        return false;
    }
    const std::uint8_t controller = event.data[1];
    return sustain_pedal_of(event) == std::optional<bool>(false) ||
           controller == kMidiAllSoundOffController || controller == kMidiAllNotesOffController;
}

std::optional<bool> sustain_pedal_of(const MidiEvent& event) noexcept VOCROZ_NONBLOCKING {
    if (!is_control_change(event) || event.data[1] != kMidiSustainController) {
        return std::nullopt;
    }
    return event.data[2] >= kSwitchOnThreshold;
}

std::uint8_t channel_of(const MidiEvent& event) noexcept VOCROZ_NONBLOCKING {
    return static_cast<std::uint8_t>(event.data[0] & 0x0FU);
}

}  // namespace vocroz::audio_core
