// SPDX-License-Identifier: AGPL-3.0-only
//
// jitter_estimator.h の実装。

#include "vocroz/transport/jitter_estimator.h"

#include <cmath>

#include "vocroz/transport/constants.h"

namespace vocroz::transport {
namespace {

// RFC 3550 の平滑化の重み。1 回の外れ値で値が跳ねないよう、16 フレームほどで追従させる。
constexpr double kSmoothing = 1.0 / 16.0;

constexpr double kMicrosecondsPerSample = 1'000'000.0 / kSampleRate;

}  // namespace

void JitterEstimator::on_frame(std::uint64_t timestamp, std::uint32_t mute_epoch,
                               TimePoint arrival) noexcept {
    const Previous current{timestamp, mute_epoch, arrival};
    if (!previous_ || previous_->mute_epoch != mute_epoch) {
        previous_ = current;
        return;
    }
    const auto arrival_us = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(arrival - previous_->arrival)
            .count());
    // 順序が逆転して届いたフレームは差が負になるので、符号付きで求める。
    const auto media_us =
        (static_cast<double>(timestamp) - static_cast<double>(previous_->timestamp)) *
        kMicrosecondsPerSample;
    const double deviation = std::abs(arrival_us - media_us);
    jitter_us_ = jitter_us_ ? *jitter_us_ + (deviation - *jitter_us_) * kSmoothing : deviation;
    previous_ = current;
}

std::optional<std::chrono::microseconds> JitterEstimator::jitter() const noexcept {
    if (!jitter_us_) {
        return std::nullopt;
    }
    return std::chrono::microseconds{static_cast<std::int64_t>(std::llround(*jitter_us_))};
}

}  // namespace vocroz::transport
