// SPDX-License-Identifier: AGPL-3.0-only
//
// sample_offset_in_block() の実装。

#include "midi_timing.h"

#include <algorithm>
#include <chrono>

#include "vocroz/audio_core/constants.h"

namespace vocroz::audio_core {
namespace {

// これより古いタイムスタンプは、どれだけ古くてもブロックの先頭に置くので、経過時間をこの値で
// 打ち切ってから計算する。サンプル数への換算（ナノ秒 × 48000）で 64
// ビットがあふれないようにするため。
constexpr std::chrono::nanoseconds kMaxLateness = std::chrono::seconds(1);

constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000;

}  // namespace

std::optional<std::uint32_t> sample_offset_in_block(
    const MidiBlockClock& clock, common::Clock::TimePoint timestamp,
    std::size_t schedule_ahead_samples) noexcept VOCROZ_NONBLOCKING {
    if (clock.frames == 0) {
        return std::nullopt;
    }
    if (!clock.has_anchor) {
        return 0;
    }
    const auto elapsed = std::clamp(
        std::chrono::duration_cast<std::chrono::nanoseconds>(timestamp - clock.anchor_time),
        -kMaxLateness, std::chrono::nanoseconds::zero());
    // 1 サンプル未満の端数は 0 の側へ切り捨てる（差は 21μs 未満で聞き分けられない）。
    const std::int64_t elapsed_samples =
        elapsed.count() * static_cast<std::int64_t>(kSampleRate) / kNanosecondsPerSecond;
    const std::int64_t target = static_cast<std::int64_t>(clock.anchor_sample) + elapsed_samples +
                                static_cast<std::int64_t>(schedule_ahead_samples);
    const std::int64_t offset = target - static_cast<std::int64_t>(clock.first_sample);
    if (offset < 0) {
        return 0;
    }
    if (offset >= static_cast<std::int64_t>(clock.frames)) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(offset);
}

}  // namespace vocroz::audio_core
