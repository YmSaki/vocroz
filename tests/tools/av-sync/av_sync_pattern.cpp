// SPDX-License-Identifier: AGPL-3.0-only
//
// 光とクリック音を同じ時間軸から生成します。
#include "av_sync_pattern.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vocroz::tools {
namespace {
// 秒とミリ秒を相互変換します。
constexpr std::int64_t kMillis = 1000;
// 長時間の記録でも積があふれないように、秒と端数に分けます。
std::int64_t index_at(std::int64_t ms, int rate) {
    return ms / kMillis * rate + ms % kMillis * rate / kMillis;
}
}  // namespace
common::Result<void, AvSyncPatternError> validate(const AvSyncPattern& p) {
    if (p.sample_rate <= 0 || p.fps <= 0 || p.seconds <= 0 || p.width <= 0 || p.height <= 0 ||
        p.width % 2 || p.height % 2 || !(p.click_amplitude > 0 && p.click_amplitude <= 1) ||
        p.flash_ms <= 0 || p.click_ms <= 0 || p.flash_ms >= p.interval_ms ||
        p.click_ms >= p.interval_ms || !std::isfinite(p.click_hz)) {
        return common::make_unexpected(AvSyncPatternError::kInvalidValue);
    }
    if ((1LL * p.flash_ms * p.fps) % kMillis || (1LL * p.interval_ms * p.fps) % kMillis) {
        return common::make_unexpected(AvSyncPatternError::kNotFrameAligned);
    }
    if (std::abs(2LL * p.audio_offset_ms) >= p.interval_ms) {
        return common::make_unexpected(AvSyncPatternError::kOffsetTooLarge);
    }
    return {};
}
std::vector<std::int64_t> event_times_ms(const AvSyncPattern& p) {
    std::vector<std::int64_t> events;
    const auto tail =
        std::max(p.flash_ms, p.click_ms) + std::abs(1LL * p.audio_offset_ms) + p.interval_ms / 2;
    for (long long t = p.interval_ms; t + tail <= p.seconds * kMillis; t += p.interval_ms) {
        events.push_back(t);
    }
    return events;
}
std::vector<float> generate_audio(const AvSyncPattern& p) {
    std::vector<float> samples(static_cast<std::size_t>(p.seconds) *
                               static_cast<std::size_t>(p.sample_rate));
    for (const auto t : event_times_ms(p)) {
        const auto start = index_at(t + p.audio_offset_ms, p.sample_rate);
        for (long long n = 0; n < 1LL * p.click_ms * p.sample_rate / kMillis; ++n) {
            samples[static_cast<std::size_t>(start + n)] = static_cast<float>(
                p.click_amplitude * std::sin(2 * std::numbers::pi * p.click_hz *
                                             static_cast<double>(n) / p.sample_rate));
        }
    }
    return samples;
}
std::vector<std::uint8_t> generate_frame_lumas(const AvSyncPattern& p) {
    std::vector<std::uint8_t> lumas(static_cast<std::size_t>(p.seconds) *
                                    static_cast<std::size_t>(p.fps));
    // 白は 8 ビットの最大輝度です。
    constexpr std::uint8_t kWhite = 255;
    for (const auto t : event_times_ms(p)) {
        std::fill_n(lumas.begin() + index_at(t, p.fps), 1LL * p.flash_ms * p.fps / kMillis, kWhite);
    }
    return lumas;
}
}  // namespace vocroz::tools
