// SPDX-License-Identifier: AGPL-3.0-only
//
// dB とリニアゲインの変換の実装。

#include "vocroz/audio_core/gain.h"

#include <cmath>
#include <limits>

#include "vocroz/audio_core/constants.h"

namespace vocroz::audio_core {

float fader_db_to_linear(float db) noexcept {
    // NaN は比較がすべて偽になるので、!(db > -inf) で -∞ と一緒に無音へ倒す。
    if (!(db > -std::numeric_limits<float>::infinity())) {
        return 0.0F;
    }
    const float clamped = db > kMaxFaderGainDb ? kMaxFaderGainDb : db;
    return std::pow(10.0F, clamped / 20.0F);
}

float linear_to_db(float linear) noexcept {
    if (!(linear > 0.0F)) {
        return -std::numeric_limits<float>::infinity();
    }
    return 20.0F * std::log10(linear);
}

}  // namespace vocroz::audio_core
