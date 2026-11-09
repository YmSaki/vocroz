// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の AudioSource。一定値か、呼び出しをまたいで連続する正弦波を返す。
// ステレオは左右で別の一定値を返し、左右が入れ替わらないことを確かめられるようにする。
// ヒープを確保しないので、RealtimeSanitizer を有効にしたテストでもそのまま使える。
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

#include "vocroz/audio_core/constants.h"
#include "vocroz/audio_core/ports/audio_source.h"

namespace vocroz::audio_core {

/// 決定的なサンプルを返すソース。
/// スレッド安全性: pull() はオーディオスレッド（テストのスレッド）だけが呼ぶ。
class FakeAudioSource final : public AudioSource {
public:
    /// 一定値を返すソースを作る。
    /// @param value 返すサンプルの値
    [[nodiscard]] static FakeAudioSource constant(float value) {
        return FakeAudioSource(value, value, 1, 0.0F);
    }

    /// 左右で別の一定値を返すステレオのソースを作る。
    /// @param left  L のサンプルの値
    /// @param right R のサンプルの値
    [[nodiscard]] static FakeAudioSource stereo(float left, float right) {
        return FakeAudioSource(left, right, 2, 0.0F);
    }

    /// 正弦波を返すソースを作る。
    /// @param amplitude 振幅
    /// @param frequency_hz 周波数（Hz）
    [[nodiscard]] static FakeAudioSource sine(float amplitude, float frequency_hz) {
        return FakeAudioSource(amplitude, amplitude, 1, frequency_hz);
    }

    [[nodiscard]] std::size_t channel_count() const noexcept override {
        return channel_count_;
    }

    void pull(float* const* channels, std::size_t frames) noexcept VOCROZ_NONBLOCKING override {
        if (frequency_hz_ == 0.0F) {
            std::fill(channels[0], channels[0] + frames, amplitude_);
            if (channel_count_ == 2) {
                std::fill(channels[1], channels[1] + frames, right_);
            }
            return;
        }
        const double step =
            2.0 * std::numbers::pi * frequency_hz_ / static_cast<double>(kSampleRate);
        for (std::size_t i = 0; i < frames; ++i) {
            channels[0][i] =
                amplitude_ * static_cast<float>(std::sin(step * static_cast<double>(position_)));
            ++position_;
        }
    }

private:
    FakeAudioSource(float amplitude, float right, std::size_t channel_count, float frequency_hz)
        : amplitude_(amplitude),
          right_(right),
          channel_count_(channel_count),
          frequency_hz_(frequency_hz) {}

    float amplitude_;
    float right_;
    std::size_t channel_count_;
    float frequency_hz_;
    unsigned long long position_ = 0;
};

}  // namespace vocroz::audio_core
