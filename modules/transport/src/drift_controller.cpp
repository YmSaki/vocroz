// SPDX-License-Identifier: AGPL-3.0-only
//
// drift_controller.h の実装。

#include "drift_controller.h"

#include <algorithm>
#include <cmath>

#include "transport_constants.h"

namespace vocroz::transport {

double DriftController::update(double depth_samples, double target_samples,
                               std::size_t elapsed_samples) noexcept {
    const double dt = static_cast<double>(elapsed_samples) / static_cast<double>(kSampleRate);
    if (!has_depth_) {
        smoothed_depth_ = depth_samples;
        has_depth_ = true;
    } else {
        // 1 次の指数移動平均。経過時間に応じた係数にして、ブロック長が変わっても時定数を保つ。
        const double alpha = 1.0 - std::exp(-dt / kDepthSmoothingSeconds);
        smoothed_depth_ += alpha * (depth_samples - smoothed_depth_);
    }
    // 誤差を秒に直す。深さが目標より大きければ正で、入力を速く消費して浅くする。
    const double error = (smoothed_depth_ - target_samples) / static_cast<double>(kSampleRate);
    integral_ = std::clamp(integral_ + kRateIntegralGain * error * dt, -kMaxDriftEstimate,
                           kMaxDriftEstimate);
    const double deviation = std::clamp(kRateProportionalGain * error + integral_,
                                        -kMaxRateDeviation, kMaxRateDeviation);
    ratio_ = 1.0 + deviation;
    return ratio_;
}

void DriftController::restart() noexcept {
    has_depth_ = false;
    smoothed_depth_ = 0.0;
    ratio_ = 1.0 + integral_;
}

}  // namespace vocroz::transport
