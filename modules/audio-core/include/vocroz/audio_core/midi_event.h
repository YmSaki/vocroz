// SPDX-License-Identifier: AGPL-3.0-only
//
// ブロック内の MIDI イベントの値型。
// プラグインノード（PluginProcessor）に音声と同じブロック単位で渡す。VSTi
// はこの型でノートを受け取る。 タイムスタンプからサンプルオフセットへの変換は
// MidiInputQueue（midi_input_queue.h）、 メッセージの判定と組み立ては midi_message.h
// が受け持つ（docs/adr/0029-midi-and-vsti.md）。
#pragma once

#include <array>
#include <cstdint>

namespace vocroz::audio_core {

/// 1 ブロックの中の、1 つの MIDI メッセージ（チャンネルメッセージ）。
/// SysEx は扱わないので、長さは 3 バイト以下に固定し、オーディオスレッドで確保なしに受け渡す。
struct MidiEvent {
    std::uint32_t sample_offset = 0;  ///< ブロックの先頭からのサンプル位置（frames 未満）
    std::array<std::uint8_t, 3> data{};  ///< ステータスバイトとデータバイト
    std::uint8_t size = 0;               ///< data のうち有効なバイト数（1〜3）
};

}  // namespace vocroz::audio_core
