// SPDX-License-Identifier: AGPL-3.0-only
//
// SafetyLimiter の実装。

#include "safety_limiter.h"

#include <algorithm>
#include <cmath>

#include "vocroz/audio_core/constants.h"

#include "audio_core_constants.h"

namespace vocroz::audio_core {

SafetyLimiter::SafetyLimiter() noexcept
    : release_coefficient_(
          1.0F - std::exp(-1.0F / (kLimiterReleaseSeconds * static_cast<float>(kSampleRate)))) {}

void SafetyLimiter::process(float* const* channels, std::size_t channel_count,
                            std::size_t frames) noexcept VOCROZ_NONBLOCKING {
    for (std::size_t i = 0; i < frames; ++i) {
        float peak = 0.0F;
        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            float& sample = channels[ch][i];
            if (!std::isfinite(sample)) {
                sample = 0.0F;
            }
            peak = std::max(peak, std::fabs(sample));
        }
        // 天井を超えるサンプルでは、そのサンプルがちょうど天井になるゲインまで即座に下げる。
        // 下げる必要がなければ、リリースの時定数で 1 へ戻す。
        const float required = peak > kLimiterCeiling ? kLimiterCeiling / peak : 1.0F;
        if (required < gain_) {
            gain_ = required;
        } else {
            gain_ += (required - gain_) * release_coefficient_;
        }
        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            channels[ch][i] *= gain_;
        }
    }
}

}  // namespace vocroz::audio_core
