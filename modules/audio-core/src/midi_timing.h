// SPDX-License-Identifier: AGPL-3.0-only
//
// MIDI のタイムスタンプ（実時間）から、ブロック内のサンプルオフセットへの変換。
// MidiInputQueue だけが使うが、規則が単独でテストしやすいので純粋関数として分けておく。
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "vocroz/audio_core/ports/midi_source.h"
#include "vocroz/audio_core/realtime.h"
#include "vocroz/common/ports/clock.h"

namespace vocroz::audio_core {

/// タイムスタンプのイベントを置くブロック内のサンプル位置を返す。
///
/// イベントを鳴らすサンプル位置は「コールバックの先頭のサンプル位置 + タイムスタンプから
/// コールバックの先頭の時刻までの経過（負） + schedule_ahead_samples」とする。
/// - 位置がブロックより前（遅れて届いた）なら 0 に置き、できるだけ早く鳴らす
/// - 位置がブロックより後なら std::nullopt を返し、呼び出し側は次のブロックまで持ち越す
/// -
/// コールバックの先頭より後のタイムスタンプ（コールバック中に届いた）は、先頭の時刻と同じとみなす。
///   未来の時刻を信じて持ち越すと、時計のずれたイベントがいつまでも鳴らず、後続も詰まるため
/// @param clock このブロックのサンプル位置と時刻の対応
/// @param timestamp イベントのタイムスタンプ
/// @param schedule_ahead_samples 発音を遅らせるサンプル数（0 なら最小遅延）
/// @return ブロック内のサンプル位置（clock.frames 未満）。このブロックより後なら std::nullopt
/// スレッド安全性: 純粋関数。オーディオスレッドから呼び出し可能。
[[nodiscard]] std::optional<std::uint32_t> sample_offset_in_block(
    const MidiBlockClock& clock, common::Clock::TimePoint timestamp,
    std::size_t schedule_ahead_samples) noexcept VOCROZ_NONBLOCKING;

}  // namespace vocroz::audio_core
