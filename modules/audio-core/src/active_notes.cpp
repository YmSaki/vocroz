// SPDX-License-Identifier: AGPL-3.0-only
//
// ActiveNotes の実装。

#include "active_notes.h"

namespace vocroz::audio_core {
namespace {

constexpr std::size_t kBitsPerWord = 64;

}  // namespace

void ActiveNotes::observe(const MidiEvent& event) noexcept VOCROZ_NONBLOCKING {
    const std::uint8_t channel = channel_of(event);
    if (is_note_on(event) || is_note_off(event)) {
        const std::uint8_t note = event.data[1];
        const std::uint64_t bit = std::uint64_t{1} << (note % kBitsPerWord);
        std::uint64_t& word = notes_[channel][note / kBitsPerWord];
        word = is_note_on(event) ? (word | bit) : (word & ~bit);
        return;
    }
    if (const auto pressed = sustain_pedal_of(event)) {
        sustain_[channel] = *pressed;
        return;
    }
    if (is_release_message(event)) {
        // All Sound Off と All Notes Off は、そのチャンネルのノートをすべて止める。
        // サステインはペダルの状態なので変えない（All Notes Off でもペダル中の音は残る）。
        notes_[channel] = {};
    }
}

bool ActiveNotes::any() const noexcept VOCROZ_NONBLOCKING {
    for (std::size_t ch = 0; ch < kMidiChannels; ++ch) {
        if (sustain_[ch]) {
            return true;
        }
        for (const std::uint64_t word : notes_[ch]) {
            if (word != 0) {
                return true;
            }
        }
    }
    return false;
}

std::size_t ActiveNotes::release_all(std::span<MidiEvent> out,
                                     std::uint32_t sample_offset) noexcept VOCROZ_NONBLOCKING {
    std::size_t count = 0;
    for (std::size_t ch = 0; ch < kMidiChannels; ++ch) {
        const auto channel = static_cast<std::uint8_t>(ch);
        for (std::size_t w = 0; w < notes_[ch].size(); ++w) {
            std::uint64_t& word = notes_[ch][w];
            for (std::size_t b = 0; b < kBitsPerWord && word != 0; ++b) {
                const std::uint64_t bit = std::uint64_t{1} << b;
                if ((word & bit) == 0) {
                    continue;
                }
                if (count == out.size()) {
                    return count;
                }
                out[count++] = make_note_off(
                    channel, static_cast<std::uint8_t>(w * kBitsPerWord + b), sample_offset);
                word &= ~bit;
            }
        }
        // ノートを止めてからペダルを離す。逆にすると、ペダルを離した瞬間に止まる音と、
        // 直後の Note Off で止まる音に分かれ、余韻の切れ方がノートごとに揃わない。
        if (sustain_[ch]) {
            if (count == out.size()) {
                return count;
            }
            out[count++] = make_control_change(channel, kMidiSustainController, 0, sample_offset);
            sustain_[ch] = false;
        }
    }
    return count;
}

void ActiveNotes::merge(const ActiveNotes& other) noexcept VOCROZ_NONBLOCKING {
    for (std::size_t ch = 0; ch < kMidiChannels; ++ch) {
        for (std::size_t w = 0; w < notes_[ch].size(); ++w) {
            notes_[ch][w] |= other.notes_[ch][w];
        }
        sustain_[ch] = sustain_[ch] || other.sustain_[ch];
    }
}

}  // namespace vocroz::audio_core
