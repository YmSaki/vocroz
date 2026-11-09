// SPDX-License-Identifier: AGPL-3.0-only
//
// PlayoutSource の実装。

#include "playout_source.h"

#include <utility>

namespace vocroz::engine {

PlayoutSource::PlayoutSource(std::unique_ptr<transport::PlayoutStream> stream) noexcept
    : stream_(std::move(stream)) {}

void PlayoutSource::pull(std::span<float> out) noexcept VOCROZ_NONBLOCKING {
    stream_->render(out);
}

}  // namespace vocroz::engine
