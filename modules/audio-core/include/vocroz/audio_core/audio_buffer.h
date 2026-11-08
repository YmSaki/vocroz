// SPDX-License-Identifier: AGPL-3.0-only
//
// デバイスとの境界で受け渡すインターリーブ形式の音声バッファ。
// デバイスのコールバックは通常インターリーブ形式（L R L R ...）でサンプルを渡すので、
// ポートのシグネチャではこの形式を使い、内部の planar 形式への変換は audio-core が持つ。
#pragma once

#include <cstddef>
#include <span>

namespace vocroz::audio_core {

/// 書き込み先のインターリーブ形式のバッファ（所有権を持たない）。
/// samples の長さは channels の倍数とする。
struct InterleavedBuffer {
    std::span<float> samples;  ///< サンプル列。フレームごとに channels 個のサンプルが並ぶ
    std::size_t channels = 0;  ///< 1 フレームあたりのチャンネル数（1 以上）

    /// フレーム数を返す。channels が 0 なら 0。
    [[nodiscard]] constexpr std::size_t frames() const noexcept {
        return channels == 0 ? 0 : samples.size() / channels;
    }
};

/// 読み取り専用のインターリーブ形式のバッファ（所有権を持たない）。
/// samples の長さは channels の倍数とする。
struct ConstInterleavedBuffer {
    std::span<const float> samples;  ///< サンプル列。フレームごとに channels 個のサンプルが並ぶ
    std::size_t channels = 0;  ///< 1 フレームあたりのチャンネル数（1 以上）

    /// フレーム数を返す。channels が 0 なら 0。
    [[nodiscard]] constexpr std::size_t frames() const noexcept {
        return channels == 0 ? 0 : samples.size() / channels;
    }
};

}  // namespace vocroz::audio_core
