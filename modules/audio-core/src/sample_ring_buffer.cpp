// SPDX-License-Identifier: AGPL-3.0-only
//
// SampleRingBuffer の実装。位置の公開と取得の順序は common::SpscQueue と同じ考え方で、
// 相手が書いた位置は acquire で読み、自分の書き込み（読み込み）を終えてから release
// で位置を進める。

#include "sample_ring_buffer.h"

#include <algorithm>

#include "audio_core_constants.h"

namespace vocroz::audio_core {

SampleRingBuffer::SampleRingBuffer(std::size_t channel_count, std::size_t capacity_frames)
    : channel_count_(std::clamp<std::size_t>(channel_count, 1, kStereoChannels)),
      capacity_frames_(std::max<std::size_t>(capacity_frames, 1)),
      samples_(capacity_frames_ * channel_count_, 0.0F) {}

std::size_t SampleRingBuffer::writable_frames() const noexcept VOCROZ_NONBLOCKING {
    const std::uint64_t written = write_position_.load(std::memory_order_relaxed);
    const std::uint64_t read = read_position_.load(std::memory_order_acquire);
    return capacity_frames_ - static_cast<std::size_t>(written - read);
}

std::size_t SampleRingBuffer::readable_frames() const noexcept VOCROZ_NONBLOCKING {
    const std::uint64_t written = write_position_.load(std::memory_order_acquire);
    const std::uint64_t read = read_position_.load(std::memory_order_relaxed);
    return static_cast<std::size_t>(written - read);
}

std::size_t SampleRingBuffer::sample_index(std::uint64_t position) const noexcept
    VOCROZ_NONBLOCKING {
    return static_cast<std::size_t>(position % capacity_frames_) * channel_count_;
}

bool SampleRingBuffer::write_planar(const float* const* channels,
                                    std::size_t frames) noexcept VOCROZ_NONBLOCKING {
    if (frames > writable_frames()) {
        return false;
    }
    const std::uint64_t start = write_position_.load(std::memory_order_relaxed);
    std::size_t index = sample_index(start);
    for (std::size_t i = 0; i < frames; ++i) {
        for (std::size_t ch = 0; ch < channel_count_; ++ch) {
            samples_[index + ch] = channels[ch][i];
        }
        index += channel_count_;
        if (index == samples_.size()) {
            index = 0;
        }
    }
    write_position_.store(start + frames, std::memory_order_release);
    return true;
}

std::size_t SampleRingBuffer::write_interleaved(std::span<const float> interleaved) noexcept {
    const std::size_t frames = std::min(interleaved.size() / channel_count_, writable_frames());
    const std::uint64_t start = write_position_.load(std::memory_order_relaxed);
    // 領域の終わりで折り返すので、最大 2 回に分けてまとめてコピーする。
    const std::size_t index = sample_index(start);
    const std::size_t total = frames * channel_count_;
    const std::size_t first = std::min(total, samples_.size() - index);
    std::copy_n(interleaved.begin(), first, samples_.begin() + static_cast<std::ptrdiff_t>(index));
    std::copy_n(interleaved.begin() + static_cast<std::ptrdiff_t>(first), total - first,
                samples_.begin());
    write_position_.store(start + frames, std::memory_order_release);
    return frames;
}

std::size_t SampleRingBuffer::read_planar(float* const* channels,
                                          std::size_t frames) noexcept VOCROZ_NONBLOCKING {
    frames = std::min(frames, readable_frames());
    const std::uint64_t start = read_position_.load(std::memory_order_relaxed);
    std::size_t index = sample_index(start);
    for (std::size_t i = 0; i < frames; ++i) {
        for (std::size_t ch = 0; ch < channel_count_; ++ch) {
            channels[ch][i] = samples_[index + ch];
        }
        index += channel_count_;
        if (index == samples_.size()) {
            index = 0;
        }
    }
    read_position_.store(start + frames, std::memory_order_release);
    return frames;
}

std::size_t SampleRingBuffer::read_interleaved(std::span<float> interleaved,
                                               std::size_t max_frames) noexcept {
    const std::size_t frames =
        std::min({max_frames, interleaved.size() / channel_count_, readable_frames()});
    const std::uint64_t start = read_position_.load(std::memory_order_relaxed);
    const std::size_t index = sample_index(start);
    const std::size_t total = frames * channel_count_;
    const std::size_t first = std::min(total, samples_.size() - index);
    std::copy_n(samples_.begin() + static_cast<std::ptrdiff_t>(index), first, interleaved.begin());
    std::copy_n(samples_.begin(), total - first,
                interleaved.begin() + static_cast<std::ptrdiff_t>(first));
    read_position_.store(start + frames, std::memory_order_release);
    return frames;
}

void SampleRingBuffer::skip_to(std::uint64_t position) noexcept VOCROZ_NONBLOCKING {
    const std::uint64_t read = read_position_.load(std::memory_order_relaxed);
    const std::uint64_t written = write_position_.load(std::memory_order_acquire);
    const std::uint64_t target = std::min(position, written);
    if (target > read) {
        read_position_.store(target, std::memory_order_release);
    }
}

}  // namespace vocroz::audio_core
