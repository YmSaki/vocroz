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

/// 入力ストリップの手動同期オフセット（ディレイ）の上限（サンプル、1 秒）。
/// 子機ごとの回線遅延の差や、映像との同期のずれを手で合わせる用途で、数百 ms 程度の差を
/// 余裕をもって吸収できる長さにする。ディレイラインはグラフの構築時に確保するので、
/// 上限を大きくするとストリップ 1 本あたりのメモリ（1 秒で 192KB）が増える。
inline constexpr std::size_t kMaxInputDelaySamples = kSampleRate;

/// 遅延ノード（フィードバック経路を許す明示的なディレイ）の遅延の下限（サンプル）。
/// ループを含むグラフは、遅延ノードの出力を前のブロックまでに書いたサンプルから作ることで
/// 処理順を決める。そのため遅延は 1 ブロック以上でなければならない。
inline constexpr std::size_t kMinFeedbackDelaySamples = kBlockSize;

/// 遅延ノードの遅延の上限（サンプル、2 秒）。エコーなどの演出で使う長さを想定する。
inline constexpr std::size_t kMaxFeedbackDelaySamples = 2 * kSampleRate;

/// プラグイン遅延補正（PDC）で足す遅延の上限（サンプル、1 秒）。プラグインが報告する遅延も
/// この値以下に限る。リニアフェイズ EQ やルックアヘッドの長いリミッターでも数百 ms に収まるので、
/// 余裕をもって 1 秒にする。補正のディレイラインはグラフの構築時に確保するので、上限を大きくすると
/// 補正する結線 1 本あたりのメモリ（1 秒、ステレオで 384KB）が増える。これを超える遅延を報告する
/// プラグインは、合奏の低遅延の要件に合わないので受け付けない。
inline constexpr std::size_t kMaxLatencyCompensationSamples = kSampleRate;

/// 出力デバイスのチャンネル数の上限。出力ノードはこの範囲のチャンネルの組に書く。
/// 多チャンネルのオーディオインターフェース（ADAT を含めて 32〜64 チャンネル）を想定する。
inline constexpr std::size_t kMaxOutputChannels = 64;

}  // namespace vocroz::audio_core
