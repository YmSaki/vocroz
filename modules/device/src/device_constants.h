// SPDX-License-Identifier: AGPL-3.0-only
//
// device モジュールの中だけで使う定数。
#pragma once

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

}  // namespace vocroz::device
