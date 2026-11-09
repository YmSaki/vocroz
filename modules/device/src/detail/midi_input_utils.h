// SPDX-License-Identifier: AGPL-3.0-only
//
// MIDI 入力のアダプターが、ライブラリから受け取ったメッセージを audio-core の型に写す純粋関数。
// JUCE に依存しないので、Linux の CI でもテストする。
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "vocroz/audio_core/midi_input_queue.h"
#include "vocroz/common/ports/clock.h"

namespace vocroz::device::detail {

/// ライブラリのミリ秒の時刻で付いたタイムスタンプを、Clock の時刻に写す。
/// 2 つの時計の差は呼び出しのたびに測り直すので、時計の進み方がずれても溜まらない。
/// @param clock_now   Clock の今の時刻
/// @param library_now_ms ライブラリの時計の今の時刻（ミリ秒）
/// @param message_ms  メッセージに付いたライブラリの時計の時刻（ミリ秒）
/// @return メッセージを受け取った Clock の時刻。未来の時刻は今に、kMaxMidiMessageAge より古い
///         時刻はその上限に丸める
[[nodiscard]] common::Clock::TimePoint to_clock_time(common::Clock::TimePoint clock_now,
                                                     double library_now_ms,
                                                     double message_ms) noexcept;

/// 生のバイト列を MidiInputQueue に積める形にする。
/// @param bytes     1 つのメッセージのバイト列
/// @param timestamp 受け取った Clock の時刻
/// @return チャンネルメッセージならその値。SysEx、システムメッセージ、長さの合わないものは
///         std::nullopt
[[nodiscard]] std::optional<audio_core::TimestampedMidiMessage> to_timestamped_message(
    std::span<const std::uint8_t> bytes, common::Clock::TimePoint timestamp) noexcept;

}  // namespace vocroz::device::detail
