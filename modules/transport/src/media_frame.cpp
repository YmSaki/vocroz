// SPDX-License-Identifier: AGPL-3.0-only
//
// media_frame.h の実装。

#include "vocroz/transport/media_frame.h"

#include <algorithm>

namespace vocroz::transport {

common::Result<MediaFrame, MediaFrameError> make_media_frame(
    std::uint64_t timestamp, std::uint32_t mute_epoch,
    std::span<const std::uint8_t> payload) noexcept {
    if (payload.empty()) {
        return common::make_unexpected(MediaFrameError::kEmptyPayload);
    }
    if (payload.size() > kMaxOpusPayloadBytes) {
        return common::make_unexpected(MediaFrameError::kPayloadTooLarge);
    }
    MediaFrame frame;
    frame.timestamp = timestamp;
    frame.mute_epoch = mute_epoch;
    frame.payload_size = static_cast<std::uint16_t>(payload.size());
    std::ranges::copy(payload, frame.payload.begin());
    return frame;
}

}  // namespace vocroz::transport
