// SPDX-License-Identifier: AGPL-3.0-only
//
// デバイスの共有モードのフォーマットとエンジンの周期から、ストリームの開き方を決める純粋関数。
// WASAPI の呼び出しから判断だけを切り出し、OS なしで単体テストできるようにする。
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "vocroz/audio_core/audio_device_error.h"
#include "vocroz/common/result.h"

namespace vocroz::device::detail {

/// デバイスの共有モードのフォーマット（ミックスフォーマット）の要約。
struct MixFormat {
    std::uint32_t sample_rate = 0;  ///< サンプルレート（Hz）
    std::size_t channels = 0;       ///< チャンネル数
    bool is_float32 = false;        ///< サンプルが 32 ビット浮動小数点なら true
};

/// オーディオエンジンが受け付ける周期（フレーム数）。IAudioClient3 で取得できたときだけある。
struct EnginePeriod {
    std::uint32_t default_frames = 0;      ///< 既定の周期
    std::uint32_t fundamental_frames = 0;  ///< 周期の刻み
    std::uint32_t min_frames = 0;          ///< 最小の周期
    std::uint32_t max_frames = 0;          ///< 最大の周期
};

/// ストリームの開き方。
enum class StreamMode {
    /// IAudioClient3 の低遅延共有モード。デバイスのフォーマットのまま、エンジンの最小周期で開く。
    kLowLatencyShared,
    /// 通常の共有モード。48kHz Float32 で開き、レートとサンプル形式の変換を OS に任せる。
    kConvertedShared,
};

/// ストリームの開き方と周期。
struct StreamPlan {
    StreamMode mode = StreamMode::kConvertedShared;
    /// kLowLatencyShared のときの周期（フレーム数）。kConvertedShared では 0（エンジンの既定）。
    std::uint32_t period_frames = 0;
};

/// ストリームの開き方を決める。
/// 低遅延共有モードは、デバイスがすでに内部フォーマット（48kHz Float32）で、IAudioClient3
/// が周期を返したときだけ選ぶ。低遅延共有モードはフォーマットの変換をしないため。
/// それ以外は通常の共有モードにし、デバイスとの境界でのリサンプリングを OS に任せる。
/// @param mix    デバイスのミックスフォーマット
/// @param period エンジンの周期。IAudioClient3 が使えなければ std::nullopt
/// @return 開き方。チャンネル数が 0 か上限を超えたら kUnsupportedFormat
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] common::Result<StreamPlan, audio_core::AudioDeviceError> plan_stream(
    const MixFormat& mix, const std::optional<EnginePeriod>& period);

}  // namespace vocroz::device::detail
