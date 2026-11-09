// SPDX-License-Identifier: AGPL-3.0-only
//
// audio-core の内部だけで使う定数。
#pragma once

#include <cstddef>

namespace vocroz::audio_core {

/// ステレオのチャンネル数。バス、リミッター、出力ノードはステレオで処理する。
inline constexpr std::size_t kStereoChannels = 2;

/// ゲインランプの長さの下限と上限（ms）。
/// 5ms 未満では急な変化でクリックノイズが聞こえ、20ms
/// を超えるとフェーダーの追従が遅れて感じられる。
inline constexpr float kMinRampDurationMs = 5.0F;
inline constexpr float kMaxRampDurationMs = 20.0F;

/// 安全リミッターの天井（リニア値、約 -1 dBFS）。
/// デバイス境界のリサンプリングと D/A 変換で生じるサンプル間ピークでクリップしないよう、
/// 0 dBFS より少し下げる。
inline constexpr float kLimiterCeiling = 0.891F;

/// 安全リミッターのリリース時間（秒）。
/// 短すぎるとゲインの揺れが歪みとして聞こえ、長すぎると大きな音の後に音量が戻るのが遅れる。
/// 一般的な安全リミッターの値として 100ms を使う。
inline constexpr float kLimiterReleaseSeconds = 0.1F;

/// RMS メーターの時定数（秒）。VU メーターの応答（約 300ms）に合わせ、
/// メーターの更新周期（10〜30 Hz）で読んでも値がばたつかないようにする。
inline constexpr float kRmsTimeConstantSeconds = 0.3F;

/// コントロールスレッドからオーディオスレッドへのパラメーター変更のキューの容量。
/// オーディオスレッドはコールバックごとにすべて取り出す。コールバック周期（最大 512 サンプル、
/// 約 10.7ms）の間にフェーダーのドラッグと一括のミュートが重なっても溢れない大きさにする。
inline constexpr std::size_t kParameterQueueCapacity = 256;

/// ディスク I/O スレッドが 1 回に書き出す、または読み込むフレーム数（約 85ms）。
/// 小さすぎるとファイルへの呼び出しの回数が増え、大きすぎると作業領域が増える。
/// WAV と FLAC のエンコーダーが効率よく処理できる 4096 フレームにする。
inline constexpr std::size_t kDiskChunkFrames = 4096;

/// 録音の欠落（リングバッファの溢れ）の位置を、オーディオスレッドからディスク I/O スレッドへ
/// 渡すキューの容量。欠落の記録 1 件の後には必ず書き込めたブロックが続くので、ディスク I/O
/// スレッドが 1 回の排出で 64 件を超えて溜めることはまずない。溢れたら次の欠落とまとめて記録する。
inline constexpr std::size_t kRecordingGapQueueCapacity = 64;

/// 再生トラックの位置の移動（シーク）を、ディスク I/O スレッドからオーディオスレッドへ渡す
/// キューの容量。オーディオスレッドはブロックごとにすべて取り出すので、少なくてよい。
inline constexpr std::size_t kPlaybackSegmentQueueCapacity = 16;

/// 再生の開始、停止、シークのフェードの長さ（サンプル、10ms）。
/// 波形の途中で音を出したり止めたりするとクリックノイズが出るので、フェーダーと同じ
/// 5〜20ms の範囲でランプを掛ける。
inline constexpr std::size_t kPlaybackFadeSamples = 480;

/// MIDI ソース 1 つから 1 ブロック（kBlockSize）に取り出すイベントの上限。
/// 1 ブロック（約 2.7ms）に MIDI 1.0 の DIN の帯域（約 1 件/ms）の 100 倍近くを受けられる。
/// 超えたぶんはソースに残り、次のブロックで送る（落とさない）。
inline constexpr std::size_t kMaxMidiEventsPerBlock = 256;

}  // namespace vocroz::audio_core
