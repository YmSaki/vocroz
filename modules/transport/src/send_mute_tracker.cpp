// SPDX-License-Identifier: AGPL-3.0-only
//
// send_mute_tracker.h の実装。

#include "vocroz/transport/send_mute_tracker.h"

namespace vocroz::transport {

MuteUpdate SendMuteTracker::apply(bool send_muted, std::uint32_t epoch) noexcept {
    // 同じ世代では解除がミュートより先に起きるので、ミュートのほうを新しいとみなす。
    const bool newer = epoch > epoch_ || (epoch == epoch_ && send_muted && !muted_);
    if (!newer) {
        return MuteUpdate::kIgnored;
    }
    epoch_ = epoch;
    muted_ = send_muted;
    return send_muted ? MuteUpdate::kMuted : MuteUpdate::kUnmuted;
}

FrameVerdict SendMuteTracker::classify(std::uint32_t epoch) noexcept {
    if (epoch > epoch_) {
        // フレームを送るのは解除中だけなので、新しい世代のフレームはその世代での解除を意味する。
        epoch_ = epoch;
        muted_ = false;
        return FrameVerdict::kAcceptNewSegment;
    }
    if (epoch == epoch_ && !muted_) {
        return FrameVerdict::kAccept;
    }
    // ミュートの通知より後に届いた、ミュート前のフレーム。ミュート中に届いたデータは受信側で破棄する。
    return FrameVerdict::kDrop;
}

}  // namespace vocroz::transport
