// SPDX-License-Identifier: AGPL-3.0-only
//
// ルーティングの遅延（ms）とサンプル数の換算の実装。

#include "vocroz/session/routing_delay.h"

#include <cmath>

#include "vocroz/audio_core/constants.h"

namespace vocroz::session {

std::size_t delay_samples(double delay_ms) noexcept {
    constexpr double kSamplesPerMs = static_cast<double>(audio_core::kSampleRate) / 1000.0;
    return static_cast<std::size_t>(std::llround(delay_ms * kSamplesPerMs));
}

}  // namespace vocroz::session
