// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の AudioStreamSink。受け取った L と R のサンプルを固定長の領域に記録する。
// ヒープを確保しないので、RealtimeSanitizer を有効にしたテストでもそのまま使える。
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

#include "vocroz/audio_core/ports/audio_stream_sink.h"

namespace vocroz::audio_core {

/// 受け取ったステレオのサンプルを記録する受け取り先。容量を超えたぶんは数だけ数えて捨てる。
/// スレッド安全性: push_stereo() はオーディオスレッド（テストのスレッド）だけが呼ぶ。
class FakeAudioStreamSink final : public AudioStreamSink {
public:
    /// 1 チャンネルあたりに記録できるサンプル数。
    static constexpr std::size_t kCapacity = 8192;

    void push_stereo(std::span<const float> left,
                     std::span<const float> right) noexcept VOCROZ_NONBLOCKING override {
        const std::size_t frames = std::min(left.size(), right.size());
        const std::size_t copied = std::min(kCapacity - recorded_, frames);
        std::copy_n(left.begin(), copied, left_.begin() + recorded_);
        std::copy_n(right.begin(), copied, right_.begin() + recorded_);
        recorded_ += copied;
        received_ += frames;
    }

    /// 記録した L のサンプル（受け取った順）。
    [[nodiscard]] std::span<const float> left() const noexcept {
        return std::span<const float>(left_).first(recorded_);
    }

    /// 記録した R のサンプル（受け取った順）。
    [[nodiscard]] std::span<const float> right() const noexcept {
        return std::span<const float>(right_).first(recorded_);
    }

    /// 最後に記録した L のサンプル。まだなければ 0。
    [[nodiscard]] float last_left() const noexcept {
        return recorded_ == 0 ? 0.0F : left_[recorded_ - 1];
    }

    /// 最後に記録した R のサンプル。まだなければ 0。
    [[nodiscard]] float last_right() const noexcept {
        return recorded_ == 0 ? 0.0F : right_[recorded_ - 1];
    }

    /// 受け取ったフレームの総数（容量を超えて捨てたぶんを含む）。
    [[nodiscard]] std::size_t received() const noexcept {
        return received_;
    }

private:
    std::array<float, kCapacity> left_{};
    std::array<float, kCapacity> right_{};
    std::size_t recorded_ = 0;
    std::size_t received_ = 0;
};

}  // namespace vocroz::audio_core
