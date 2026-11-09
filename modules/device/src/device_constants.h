// SPDX-License-Identifier: AGPL-3.0-only
//
// device モジュールの中だけで使う定数。
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace vocroz::device {

/// 1 台のデバイスで扱うチャンネル数の上限。
/// 一般的なオーディオインターフェース（ADAT を含めて 32〜64 チャンネル）を収め、
/// ドライバーが異常な値を返したときに巨大なバッファを確保しないための上限。
inline constexpr std::size_t kMaxDeviceChannels = 64;

/// デバイスのイベントを待つ時間の上限（ミリ秒）。
/// USB のデバイスを抜くと WASAPI のイベントが届かなくなり、待ち続けると抜去に気付けない。
/// この時間イベントが届かなければバッファの状態を問い合わせ、AUDCLNT_E_DEVICE_INVALIDATED
/// で抜去を検知する。通常のデバイス周期（3〜10ms）より十分長く、検知の遅れが耳で気にならない値。
inline constexpr std::uint32_t kStreamWatchdogMilliseconds = 100;

/// MIDI のタイムスタンプとして信じる、受信からの経過時間の上限。
/// ドライバーが古い時刻や壊れた時刻を付けても、MidiInputQueue の位置の計算（1 秒より古いものは
/// ブロックの先頭）を外れた値で乱さないための上限。
inline constexpr std::chrono::milliseconds kMaxMidiMessageAge{1000};

}  // namespace vocroz::device
