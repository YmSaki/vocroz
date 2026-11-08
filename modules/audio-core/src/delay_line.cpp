// SPDX-License-Identifier: AGPL-3.0-only
//
// DelayLine の実装。

#include "delay_line.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace vocroz::audio_core {

DelayLine::DelayLine(std::size_t delay_samples) : buffer_(delay_samples, 0.0F) {}

void DelayLine::process(std::span<float> samples) noexcept VOCROZ_NONBLOCKING {
    if (buffer_.empty()) {
        return;
    }
    for (float& sample : samples) {
        std::swap(sample, buffer_[position_]);
        position_ = position_ + 1 == buffer_.size() ? 0 : position_ + 1;
    }
}

void DelayLine::read(std::span<float> out) const noexcept VOCROZ_NONBLOCKING {
    const std::size_t readable = std::min(out.size(), buffer_.size());
    std::size_t position = position_;
    for (std::size_t i = 0; i < readable; ++i) {
        out[i] = buffer_[position];
        position = position + 1 == buffer_.size() ? 0 : position + 1;
    }
    std::fill(out.begin() + static_cast<std::ptrdiff_t>(readable), out.end(), 0.0F);
}

void DelayLine::write(std::span<const float> in) noexcept VOCROZ_NONBLOCKING {
    if (buffer_.empty()) {
        return;
    }
    const std::size_t writable = std::min(in.size(), buffer_.size());
    for (std::size_t i = 0; i < writable; ++i) {
        buffer_[position_] = in[i];
        position_ = position_ + 1 == buffer_.size() ? 0 : position_ + 1;
    }
}

}  // namespace vocroz::audio_core
