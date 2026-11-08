// SPDX-License-Identifier: AGPL-3.0-only
//
// stream_plan.h の実装。

#include "detail/stream_plan.h"

#include "vocroz/audio_core/constants.h"

#include "device_constants.h"

namespace vocroz::device::detail {

common::Result<StreamPlan, audio_core::AudioDeviceError> plan_stream(
    const MixFormat& mix, const std::optional<EnginePeriod>& period) {
    if (mix.channels == 0 || mix.channels > kMaxDeviceChannels) {
        return common::make_unexpected(audio_core::AudioDeviceError::kUnsupportedFormat);
    }
    const bool is_internal_format = mix.sample_rate == audio_core::kSampleRate && mix.is_float32;
    // 最小周期が 0 のドライバーは周期を正しく報告していないので、低遅延共有モードを使わない。
    if (is_internal_format && period && period->min_frames > 0) {
        return StreamPlan{StreamMode::kLowLatencyShared, period->min_frames};
    }
    return StreamPlan{StreamMode::kConvertedShared, 0};
}

}  // namespace vocroz::device::detail
