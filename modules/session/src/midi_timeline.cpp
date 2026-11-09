// SPDX-License-Identifier: AGPL-3.0-only
//
// MidiTimeline の実装。

#include "midi_timeline.h"

#include <algorithm>

#include "session_constants.h"

namespace vocroz::session {
namespace {

constexpr std::int64_t kWrapPeriod = std::int64_t{1} << 32;

}  // namespace

std::int64_t MidiTimeline::unwrap(std::uint32_t sender_time_us) noexcept {
    // 前回より小さく、差が半周を超えていれば一周したとみなす。信頼制御層は届いた順を保つので、
    // 本当に戻ることはない（戻って見えるのは桁あふれだけ）。
    if (last_sender_ && sender_time_us < *last_sender_ &&
        *last_sender_ - sender_time_us > kWrapPeriod / 2) {
        ++wraps_;
    }
    last_sender_ = sender_time_us;
    return wraps_ * kWrapPeriod + sender_time_us;
}

common::Clock::TimePoint MidiTimeline::map(std::uint32_t sender_time_us,
                                           common::Clock::TimePoint arrival) noexcept {
    const Microseconds sender{unwrap(sender_time_us)};
    const auto arrival_us = std::chrono::duration_cast<Microseconds>(arrival.time_since_epoch());
    const Microseconds difference = arrival_us - sender;

    if (!window_start_ || arrival - *window_start_ >= kMidiTimelineWindow) {
        previous_min_ = current_min_;
        current_min_.reset();
        window_start_ = arrival;
    }
    current_min_ = current_min_ ? std::min(*current_min_, difference) : difference;
    const Microseconds offset =
        previous_min_ ? std::min(*current_min_, *previous_min_) : *current_min_;
    // offset は今回の差以下なので、写した時刻（sender + offset）は受信時刻より後にならない。
    return common::Clock::TimePoint{
        std::chrono::duration_cast<common::Clock::TimePoint::duration>(sender + offset)};
}

void MidiTimeline::reset() noexcept {
    *this = MidiTimeline{};
}

}  // namespace vocroz::session
