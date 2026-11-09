// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の AudioSink。受け取ったサンプルを固定長の領域に記録する。
// ヒープを確保しないので、RealtimeSanitizer を有効にしたテストでもそのまま使える。
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

#include "vocroz/audio_core/ports/audio_sink.h"

namespace vocroz::audio_core {

/// 受け取ったサンプルを記録する受け取り先。容量を超えたぶんは数だけ数えて捨てる。
/// スレッド安全性: push() はオーディオスレッド（テストのスレッド）だけが呼ぶ。
class FakeAudioSink final : public AudioSink {
public:
    /// 記録できるサンプル数。
    static constexpr std::size_t kCapacity = 8192;

    void push(std::span<const float> samples) noexcept VOCROZ_NONBLOCKING override {
        const std::size_t room = kCapacity - recorded_;
        const std::size_t copied = std::min(room, samples.size());
        std::copy_n(samples.begin(), copied, samples_.begin() + recorded_);
        recorded_ += copied;
        received_ += samples.size();
    }

    /// 記録したサンプル（受け取った順）。
    [[nodiscard]] std::span<const float> samples() const noexcept {
        return std::span<const float>(samples_).first(recorded_);
    }

    /// 最後に記録したサンプル。まだなければ 0。
    [[nodiscard]] float last() const noexcept {
        return recorded_ == 0 ? 0.0F : samples_[recorded_ - 1];
    }

    /// 受け取ったサンプルの総数（容量を超えて捨てたぶんを含む）。
    [[nodiscard]] std::size_t received() const noexcept {
        return received_;
    }

private:
    std::array<float, kCapacity> samples_{};
    std::size_t recorded_ = 0;
    std::size_t received_ = 0;
};

}  // namespace vocroz::audio_core
