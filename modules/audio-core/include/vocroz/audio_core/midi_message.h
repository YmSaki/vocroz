// SPDX-License-Identifier: AGPL-3.0-only
//
// MIDI のチャンネルメッセージの判定と組み立て。
// 発音中のノートの追跡（audio-core）、MIDI デバイスの入力（device）、子機からの MIDI の受信
// （transport、session）が、同じ規則でメッセージを検証して解釈するように、規則をここに集める。
// ベロシティ 0 の Note On を Note Off とみなすのは MIDI 1.0
// の規約で、送り手によってどちらも使われる。
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "vocroz/audio_core/midi_event.h"
#include "vocroz/audio_core/realtime.h"

namespace vocroz::audio_core {

/// MIDI のチャンネル数。
inline constexpr std::size_t kMidiChannels = 16;
/// MIDI のノート番号の数（0〜127）。
inline constexpr std::size_t kMidiNotes = 128;
/// サステインペダル（ダンパー）のコントロールチェンジ番号。
inline constexpr std::uint8_t kMidiSustainController = 64;
/// All Sound Off のコントロールチェンジ番号（チャンネルモードメッセージ）。
inline constexpr std::uint8_t kMidiAllSoundOffController = 120;
/// All Notes Off のコントロールチェンジ番号（チャンネルモードメッセージ）。
inline constexpr std::uint8_t kMidiAllNotesOffController = 123;

/// ステータスバイトから、チャンネルメッセージ全体の長さ（バイト）を返す。
/// @param status ステータスバイト
/// @return 1〜3。チャンネルメッセージ（0x80〜0xEF）でなければ 0
/// スレッド安全性: 純粋関数。オーディオスレッドから呼び出し可能。
[[nodiscard]] std::size_t channel_message_size(std::uint8_t status) noexcept VOCROZ_NONBLOCKING;

/// バイト列が 1 つの正しいチャンネルメッセージかを返す。
/// SysEx、システムコモン、システムリアルタイム、ランニングステータスは受け付けない。
/// 外部（MIDI デバイス、子機）から受け取ったバイト列は、MidiEvent にする前にこれで検証する。
/// @param bytes 検証するバイト列
/// @return ステータスバイトが 0x80〜0xEF で、長さがステータスに合い、データバイトが 0x80 未満なら
/// true スレッド安全性: 純粋関数。オーディオスレッドから呼び出し可能。
[[nodiscard]] bool is_channel_message(std::span<const std::uint8_t> bytes) noexcept
    VOCROZ_NONBLOCKING;

/// 検証済みのバイト列から MidiEvent を作る。
/// @param bytes is_channel_message() が true を返したバイト列
/// @param sample_offset ブロックの先頭からのサンプル位置
/// @return MidiEvent。bytes が不正なら size が 0 のイベント
/// スレッド安全性: 純粋関数。オーディオスレッドから呼び出し可能。
[[nodiscard]] MidiEvent make_midi_event(std::span<const std::uint8_t> bytes,
                                        std::uint32_t sample_offset = 0) noexcept
    VOCROZ_NONBLOCKING;

/// Note Off を作る。ベロシティは中央値の 64 にする（リリースベロシティを持たない操作のため）。
/// @param channel チャンネル（0〜15。範囲外は下位 4 ビットを使う）
/// @param note ノート番号（0〜127。範囲外は下位 7 ビットを使う）
/// @param sample_offset ブロックの先頭からのサンプル位置
/// スレッド安全性: 純粋関数。オーディオスレッドから呼び出し可能。
[[nodiscard]] MidiEvent make_note_off(std::uint8_t channel, std::uint8_t note,
                                      std::uint32_t sample_offset = 0) noexcept VOCROZ_NONBLOCKING;

/// コントロールチェンジを作る。
/// @param channel チャンネル（0〜15。範囲外は下位 4 ビットを使う）
/// @param controller コントロール番号（0〜127。範囲外は下位 7 ビットを使う）
/// @param value 値（0〜127。範囲外は下位 7 ビットを使う）
/// @param sample_offset ブロックの先頭からのサンプル位置
/// スレッド安全性: 純粋関数。オーディオスレッドから呼び出し可能。
[[nodiscard]] MidiEvent make_control_change(
    std::uint8_t channel, std::uint8_t controller, std::uint8_t value,
    std::uint32_t sample_offset = 0) noexcept VOCROZ_NONBLOCKING;

/// 発音を始める Note On（ベロシティが 1 以上）なら true を返す。
/// スレッド安全性: 純粋関数。オーディオスレッドから呼び出し可能。
[[nodiscard]] bool is_note_on(const MidiEvent& event) noexcept VOCROZ_NONBLOCKING;

/// 発音を止める Note Off（0x80、またはベロシティ 0 の Note On）なら true を返す。
/// スレッド安全性: 純粋関数。オーディオスレッドから呼び出し可能。
[[nodiscard]] bool is_note_off(const MidiEvent& event) noexcept VOCROZ_NONBLOCKING;

/// コントロールチェンジなら true を返す。
/// スレッド安全性: 純粋関数。オーディオスレッドから呼び出し可能。
[[nodiscard]] bool is_control_change(const MidiEvent& event) noexcept VOCROZ_NONBLOCKING;

/// サステインペダル（コントロールチェンジ 64）のメッセージなら、ペダルを踏んだ（値が 64
/// 以上）かを返す。
/// @return 踏んだら true、離したら false。サステインペダルのメッセージでなければ std::nullopt
/// スレッド安全性: 純粋関数。オーディオスレッドから呼び出し可能。
[[nodiscard]] std::optional<bool> sustain_pedal_of(const MidiEvent& event) noexcept
    VOCROZ_NONBLOCKING;

/// 発音中のノートを止める側のメッセージ（Note Off、サステインのオフ、All Sound Off、
/// All Notes Off）なら true を返す。落とすと音残り（Stuck Note）になるメッセージの判定に使う。
/// スレッド安全性: 純粋関数。オーディオスレッドから呼び出し可能。
[[nodiscard]] bool is_release_message(const MidiEvent& event) noexcept VOCROZ_NONBLOCKING;

/// チャンネル（0〜15）を返す。
/// スレッド安全性: 純粋関数。オーディオスレッドから呼び出し可能。
[[nodiscard]] std::uint8_t channel_of(const MidiEvent& event) noexcept VOCROZ_NONBLOCKING;

}  // namespace vocroz::audio_core
