// SPDX-License-Identifier: AGPL-3.0-only
//
// midi_input_utils.h の実装。

#include "detail/midi_input_utils.h"

#include <algorithm>
#include <chrono>

#include "vocroz/audio_core/midi_message.h"

#include "device_constants.h"

namespace vocroz::device::detail {

common::Clock::TimePoint to_clock_time(common::Clock::TimePoint clock_now, double library_now_ms,
                                       double message_ms) noexcept {
    const double max_age_ms = std::chrono::duration<double, std::milli>(kMaxMidiMessageAge).count();
    const double age_ms = std::clamp(library_now_ms - message_ms, 0.0, max_age_ms);
    return clock_now - std::chrono::duration_cast<common::Clock::TimePoint::duration>(
                           std::chrono::duration<double, std::milli>(age_ms));
}

std::optional<audio_core::TimestampedMidiMessage> to_timestamped_message(
    std::span<const std::uint8_t> bytes, common::Clock::TimePoint timestamp) noexcept {
    if (!audio_core::is_channel_message(bytes)) {
        return std::nullopt;
    }
    audio_core::TimestampedMidiMessage message;
    message.timestamp = timestamp;
    std::copy(bytes.begin(), bytes.end(), message.data.begin());
    message.size = static_cast<std::uint8_t>(bytes.size());
    return message;
}

}  // namespace vocroz::device::detail
