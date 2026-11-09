// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の MediaFrameSink。受け取ったフレームを固定長の領域に記録する。満杯を模擬できる。
// ヒープを確保しないので、RealtimeSanitizer を有効にしたテストでもそのまま使える。
#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "vocroz/transport/ports/media_frame_sink.h"

namespace vocroz::transport {

/// 受け取ったフレームを記録する渡し先。
/// スレッド安全性: 1 つのスレッドから使う。
class FakeMediaFrameSink final : public MediaFrameSink {
public:
    /// 記録できるフレーム数。容量を超えたら満杯として扱う。
    static constexpr std::size_t kCapacity = 256;

    [[nodiscard]] bool push_frame(const MediaFrame& frame) noexcept VOCROZ_NONBLOCKING override {
        if (full_ || count_ == kCapacity) {
            return false;
        }
        frames_[count_] = frame;
        ++count_;
        return true;
    }

    /// 受け取ったフレーム（受け取った順）。
    [[nodiscard]] std::span<const MediaFrame> frames() const noexcept {
        return std::span<const MediaFrame>(frames_).first(count_);
    }

    void clear() noexcept {
        count_ = 0;
    }

    /// true にすると、以後のフレームを受け取らない（送信待ちの満杯）。
    void set_full(bool full) noexcept {
        full_ = full;
    }

private:
    std::array<MediaFrame, kCapacity> frames_{};
    std::size_t count_ = 0;
    bool full_ = false;
};

}  // namespace vocroz::transport
