// SPDX-License-Identifier: AGPL-3.0-only
//
// audio-core の内部フォーマットを表す定数。
// transport（Opus のフレーム長、ジッターバッファ）と device（デバイス境界のリサンプリング）も
// 同じ値を前提にするので、公開ヘッダーで 1 か所に定義する。
#pragma once

#include <cstddef>

namespace vocroz::audio_core {

/// エンジン内部のサンプルレート（Hz）。
/// メディアのタイムスタンプを入力デバイスのレートに関わらず 48kHz の時計で進めるため、
/// エンジン内はすべてこのレートで扱い、デバイスのレートが違う場合はデバイス境界で変換する。
inline constexpr std::size_t kSampleRate = 48'000;

/// 内部処理の標準ブロック長（サンプル）。
/// 48kHz で約 2.7ms。デバイスのコールバック長（64〜512 サンプルなど）とは独立で、
/// コールバック長がこれと違っても、Mixer はこの長さ以下に分割して処理する。
inline constexpr std::size_t kBlockSize = 128;

/// フェーダーの上限（dB）。フェーダーの調整範囲は -∞ dB 〜 +10 dB。
inline constexpr float kMaxFaderGainDb = 10.0F;

}  // namespace vocroz::audio_core
