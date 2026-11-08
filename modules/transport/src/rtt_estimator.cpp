// SPDX-License-Identifier: AGPL-3.0-only
//
// RFC 6298 の再送タイムアウトの計算。

#include "rtt_estimator.h"

#include <algorithm>

#include "transport_constants.h"

namespace vocroz::transport {

void RttEstimator::add_sample(Duration rtt) noexcept {
    if (!has_sample_) {
        smoothed_rtt_ = rtt;
        rtt_variation_ = rtt / 2;
        has_sample_ = true;
        return;
    }
    // RFC 6298 の係数（alpha = 1/8、beta = 1/4）。変動を先に更新するのも RFC の順序に従う。
    const auto deviation = smoothed_rtt_ > rtt ? smoothed_rtt_ - rtt : rtt - smoothed_rtt_;
    rtt_variation_ = (rtt_variation_ * 3 + deviation) / 4;
    smoothed_rtt_ = (smoothed_rtt_ * 7 + rtt) / 8;
}

RttEstimator::Duration RttEstimator::retransmit_timeout() const noexcept {
    if (!has_sample_) {
        return kInitialRetransmitTimeout;
    }
    const Duration timeout = smoothed_rtt_ + rtt_variation_ * 4;
    return std::clamp<Duration>(timeout, kMinRetransmitTimeout, kMaxRetransmitTimeout);
}

}  // namespace vocroz::transport
