// SPDX-License-Identifier: AGPL-3.0-only
//
// MeterBank の実装。

#include "meter_bank.h"

#include <algorithm>
#include <cmath>

#include "vocroz/audio_core/constants.h"

#include "audio_core_constants.h"

namespace vocroz::audio_core {

MeterBank::MeterBank(std::size_t capacity)
    : capacity_(capacity), slots_(std::make_unique<Slot[]>(capacity)) {}

void MeterBank::measure(MeterId meter, const float* const* channels, std::size_t channel_count,
                        std::size_t frames) noexcept VOCROZ_NONBLOCKING {
    if (meter.value >= capacity_ || frames == 0 || channel_count == 0) {
        return;
    }
    Slot& slot = slots_[meter.value];

    float peak = 0.0F;
    float sum_of_squares = 0.0F;
    for (std::size_t ch = 0; ch < channel_count; ++ch) {
        for (std::size_t i = 0; i < frames; ++i) {
            const float sample = channels[ch][i];
            peak = std::max(peak, std::fabs(sample));
            sum_of_squares += sample * sample;
        }
    }
    const float block_mean_square = sum_of_squares / static_cast<float>(frames * channel_count);

    // ブロック長がコールバックごとに変わっても時定数が変わらないよう、係数をブロック長から求める。
    const float alpha =
        1.0F - std::exp(-static_cast<float>(frames) /
                        (kRmsTimeConstantSeconds * static_cast<float>(kSampleRate)));
    slot.mean_square_state += (block_mean_square - slot.mean_square_state) * alpha;
    slot.mean_square.store(slot.mean_square_state, std::memory_order_relaxed);

    // 読み取り側が exchange でリセットするので、書き込み側は CAS で最大値だけを残す。
    // 競合する相手は読み取り（10〜30 Hz）だけなので、ループはほぼ 1 回で終わる。
    float previous = slot.peak_hold.load(std::memory_order_relaxed);
    while (peak > previous &&
           !slot.peak_hold.compare_exchange_weak(previous, peak, std::memory_order_relaxed)) {
    }
}

MeterReading MeterBank::read(MeterId meter) noexcept {
    if (meter.value >= capacity_) {
        return {};
    }
    Slot& slot = slots_[meter.value];
    MeterReading reading;
    reading.peak = slot.peak_hold.exchange(0.0F, std::memory_order_relaxed);
    reading.rms = std::sqrt(slot.mean_square.load(std::memory_order_relaxed));
    return reading;
}

}  // namespace vocroz::audio_core
