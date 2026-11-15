// SPDX-License-Identifier: AGPL-3.0-only
//
// PlayoutSource の実装。

#include "playout_source.h"

#include <span>
#include <utility>

namespace vocroz::engine {

PlayoutSource::PlayoutSource(std::unique_ptr<transport::PlayoutStream> stream) noexcept
    : stream_(std::move(stream)) {}

std::size_t PlayoutSource::channel_count() const noexcept {
    return stream_->channels();
}

void PlayoutSource::pull(float* const* channels, std::size_t frames) noexcept VOCROZ_NONBLOCKING {
    if (stream_->channels() == 2) {
        stream_->render(std::span<float>(channels[0], frames),
                        std::span<float>(channels[1], frames));
    } else {
        stream_->render(std::span<float>(channels[0], frames));
    }
}

bool PlayoutSource::receive(const transport::MediaFrame& frame,
                            common::Clock::TimePoint arrival) noexcept {
    estimator_.on_frame(frame.timestamp, frame.mute_epoch, arrival);
    if (const auto jitter = estimator_.jitter()) {
        jitter_us_.store(jitter->count(), std::memory_order_relaxed);
    }
    return stream_->enqueue(frame);
}

std::optional<std::chrono::microseconds> PlayoutSource::jitter() const noexcept {
    const std::int64_t us = jitter_us_.load(std::memory_order_relaxed);
    if (us == kNoJitter) {
        return std::nullopt;
    }
    return std::chrono::microseconds{us};
}

}  // namespace vocroz::engine
