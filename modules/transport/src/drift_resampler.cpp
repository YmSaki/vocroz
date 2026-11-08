// SPDX-License-Identifier: AGPL-3.0-only
//
// drift_resampler.h の実装。

#include "drift_resampler.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vocroz::transport {

namespace {

// 遮断周波数（ナイキスト周波数に対する比）。
// Opus の帯域の上限（20kHz）を平坦に通しつつ、ナイキスト周波数の直下で係数を絞って、
// 小数位相による高域の特性の揺れを小さくする。
constexpr double kCutoff = 0.95;
// カイザー窓のβ。
// 32 タップで阻止域を約 -70dB にし、通過域の平坦さとのつり合いを取る。
constexpr double kKaiserBeta = 7.0;

// 第 1 種変形ベッセル関数 I0 の級数展開。
// 係数表の生成（オーディオスレッド外）でだけ使う。
double bessel_i0(double x) {
    double sum = 1.0;
    double term = 1.0;
    const double half = x / 2.0;
    for (int k = 1; k < 50; ++k) {
        term *= half / k;
        sum += term * term;
        if (term * term < sum * 1e-17) {
            break;
        }
    }
    return sum;
}

double windowed_sinc(double x, double half_width) {
    const double ratio = x / half_width;
    if (std::abs(ratio) >= 1.0) {
        return 0.0;
    }
    const double window =
        bessel_i0(kKaiserBeta * std::sqrt(1.0 - ratio * ratio)) / bessel_i0(kKaiserBeta);
    const double arg = std::numbers::pi * kCutoff * x;
    const double sinc = std::abs(arg) < 1e-12 ? 1.0 : std::sin(arg) / arg;
    return kCutoff * sinc * window;
}

}  // namespace

DriftResampler::DriftResampler(std::size_t max_push_samples)
    : kernels_(make_kernels()), fifo_(max_push_samples * 2 + kTaps * 4) {
    reset();
}

std::array<DriftResampler::Kernel, DriftResampler::kPhases + 1> DriftResampler::make_kernels() {
    std::array<Kernel, kPhases + 1> kernels{};
    const auto half = static_cast<double>(kHalfTaps);
    for (std::size_t phase = 0; phase <= kPhases; ++phase) {
        const double fraction = static_cast<double>(phase) / static_cast<double>(kPhases);
        double sum = 0.0;
        std::array<double, kTaps> taps{};
        for (std::size_t m = 0; m < kTaps; ++m) {
            // タップ m は入力の位置 index - (kHalfTaps - 1) + m に掛ける。
            const double offset = static_cast<double>(m) - (half - 1.0) - fraction;
            taps[m] = windowed_sinc(offset, half);
            sum += taps[m];
        }
        // 直流の利得を位相によらず 1 にそろえ、位相の変化で音量が揺れないようにする。
        for (std::size_t m = 0; m < kTaps; ++m) {
            kernels[phase][m] = static_cast<float>(taps[m] / sum);
        }
    }
    return kernels;
}

bool DriftResampler::push(std::span<const float> input) noexcept {
    if (size_ + input.size() > fifo_.size()) {
        compact();
        if (size_ + input.size() > fifo_.size()) {
            return false;
        }
    }
    std::ranges::copy(input, fifo_.begin() + static_cast<std::ptrdiff_t>(size_));
    size_ += input.size();
    return true;
}

std::size_t DriftResampler::pull(std::span<float> out, double ratio) noexcept {
    std::size_t written = 0;
    while (written < out.size() && index_ + kHalfTaps < size_) {
        const double position = fraction_ * static_cast<double>(kPhases);
        const auto phase = std::min(static_cast<std::size_t>(position), kPhases - 1);
        const auto blend = static_cast<float>(position - static_cast<double>(phase));
        const Kernel& lower = kernels_[phase];
        const Kernel& upper = kernels_[phase + 1];
        const float* x = fifo_.data() + (index_ - (kHalfTaps - 1));
        float sum = 0.0F;
        for (std::size_t m = 0; m < kTaps; ++m) {
            sum += x[m] * (lower[m] + blend * (upper[m] - lower[m]));
        }
        out[written++] = sum;

        fraction_ += ratio;
        const double whole = std::floor(fraction_);
        index_ += static_cast<std::size_t>(whole);
        fraction_ -= whole;
    }
    return written;
}

double DriftResampler::buffered_samples() const noexcept {
    if (index_ >= size_) {
        return 0.0;
    }
    return static_cast<double>(size_ - index_) - fraction_;
}

void DriftResampler::reset() noexcept {
    // 過去の入力を無音とみなし、最初の出力に必要な片側ぶんの履歴を 0 で埋める。
    std::fill_n(fifo_.begin(), kHalfTaps - 1, 0.0F);
    size_ = kHalfTaps - 1;
    index_ = kHalfTaps - 1;
    fraction_ = 0.0;
}

void DriftResampler::compact() noexcept {
    // 次の出力に必要な最も古い入力を先頭に移す。
    const std::size_t start = index_ - (kHalfTaps - 1);
    if (start == 0) {
        return;
    }
    std::copy(fifo_.begin() + static_cast<std::ptrdiff_t>(start),
              fifo_.begin() + static_cast<std::ptrdiff_t>(size_), fifo_.begin());
    size_ -= start;
    index_ -= start;
}

}  // namespace vocroz::transport
