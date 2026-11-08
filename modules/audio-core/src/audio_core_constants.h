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

}  // namespace vocroz::audio_core
