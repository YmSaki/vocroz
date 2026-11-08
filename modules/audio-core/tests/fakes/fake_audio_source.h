// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の AudioSource。一定値か、呼び出しをまたいで連続する正弦波を返す。
// ヒープを確保しないので、RealtimeSanitizer を有効にしたテストでもそのまま使える。
#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>

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
        return FakeAudioSource(value, 0.0F);
    }

    /// 正弦波を返すソースを作る。
    /// @param amplitude 振幅
    /// @param frequency_hz 周波数（Hz）
    [[nodiscard]] static FakeAudioSource sine(float amplitude, float frequency_hz) {
        return FakeAudioSource(amplitude, frequency_hz);
    }

    void pull(std::span<float> out) noexcept VOCROZ_NONBLOCKING override {
        if (frequency_hz_ == 0.0F) {
            std::fill(out.begin(), out.end(), amplitude_);
            return;
        }
        const double step =
            2.0 * std::numbers::pi * frequency_hz_ / static_cast<double>(kSampleRate);
        for (float& sample : out) {
            sample =
                amplitude_ * static_cast<float>(std::sin(step * static_cast<double>(position_)));
            ++position_;
        }
    }

private:
    FakeAudioSource(float amplitude, float frequency_hz)
        : amplitude_(amplitude), frequency_hz_(frequency_hz) {}

    float amplitude_;
    float frequency_hz_;
    unsigned long long position_ = 0;
};

}  // namespace vocroz::audio_core
