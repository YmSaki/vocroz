// SPDX-License-Identifier: AGPL-3.0-only
//
// ReturnPlayer の実装。

#include "return_player.h"

#include <algorithm>
#include <cstddef>
#include <span>

namespace vocroz::client {

void ReturnPlayer::render(audio_core::InterleavedBuffer out) noexcept VOCROZ_NONBLOCKING {
    const std::size_t channels = out.channels;
    const std::size_t frames = out.frames();
    std::size_t done = 0;
    while (done < frames) {
        const std::size_t count = std::min(frames - done, mono_.size());
        const auto mono = std::span(mono_).first(count);
        returns_.render(mono);
        float* frame = out.samples.data() + (done * channels);
        for (const float sample : mono) {
            std::fill_n(frame, channels, sample);
            frame += channels;
        }
        done += count;
    }
    // channels の倍数でない端のサンプルは、どのフレームにも属さないので無音にする。
    std::fill(out.samples.begin() + static_cast<std::ptrdiff_t>(frames * channels),
              out.samples.end(), 0.0F);
}

}  // namespace vocroz::client
