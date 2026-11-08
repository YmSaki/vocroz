// SPDX-License-Identifier: AGPL-3.0-only
//
// GainRamp の実装。

#include "vocroz/audio_core/gain_ramp.h"

#include <algorithm>

namespace vocroz::audio_core {

GainRamp::GainRamp(std::size_t ramp_samples) noexcept
    : ramp_samples_(std::max<std::size_t>(ramp_samples, 1)) {}

void GainRamp::set_target(float target) noexcept VOCROZ_NONBLOCKING {
    if (target == target_ && remaining_ == 0) {
        return;
    }
    target_ = target;
    remaining_ = ramp_samples_;
    step_ = (target_ - current_) / static_cast<float>(ramp_samples_);
}

void GainRamp::jump_to(float value) noexcept VOCROZ_NONBLOCKING {
    current_ = value;
    target_ = value;
    step_ = 0.0F;
    remaining_ = 0;
}

float GainRamp::next() noexcept VOCROZ_NONBLOCKING {
    const float gain = current_;
    if (remaining_ > 0) {
        --remaining_;
        // 最後のサンプルで目標値に合わせ、浮動小数点の誤差を積み残さない。
        current_ = remaining_ == 0 ? target_ : current_ + step_;
    }
    return gain;
}

void GainRamp::apply(float* const* channels, std::size_t channel_count,
                     std::size_t frames) noexcept VOCROZ_NONBLOCKING {
    std::size_t frame = 0;
    // ランプ中はサンプルごとにゲインを変える。
    for (; frame < frames && remaining_ > 0; ++frame) {
        const float gain = next();
        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            channels[ch][frame] *= gain;
        }
    }
    if (frame == frames) {
        return;
    }
    // 到達後は一定のゲインになる。1 なら何もしなくてよい。
    if (current_ == 1.0F) {
        return;
    }
    for (std::size_t ch = 0; ch < channel_count; ++ch) {
        float* samples = channels[ch];
        if (current_ == 0.0F) {
            std::fill(samples + frame, samples + frames, 0.0F);
        } else {
            for (std::size_t i = frame; i < frames; ++i) {
                samples[i] *= current_;
            }
        }
    }
}

}  // namespace vocroz::audio_core
