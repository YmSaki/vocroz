// SPDX-License-Identifier: AGPL-3.0-only
//
// Master の後段に置く安全リミッター。
// 入力の過大や DSP の不具合で、出力に天井を超える音や不正な値（NaN、無限大）が出ないようにする。
// 低レイテンシを保つため先読み（ルックアヘッド）をせず、アタックは即時、リリースは指数関数で戻す。
#pragma once

#include <cstddef>

#include "vocroz/audio_core/realtime.h"

namespace vocroz::audio_core {

/// チャンネル間でゲインを連動させるピークリミッター。
/// スレッド安全性: 1 つのインスタンスを 1 つのスレッド（オーディオスレッド）だけが使う。
class SafetyLimiter {
public:
    SafetyLimiter() noexcept;

    /// planar 形式のバッファを処理する。出力の絶対値は天井（約 -1 dBFS）を超えない。
    /// NaN と無限大のサンプルは 0 に置き換える（フェイルセーフ）。
    /// @param channels 各チャンネルの先頭へのポインターの配列
    /// @param channel_count チャンネル数
    /// @param frames 処理するサンプル数
    void process(float* const* channels, std::size_t channel_count,
                 std::size_t frames) noexcept VOCROZ_NONBLOCKING;

    /// 現在のゲイン（1 ならリミットしていない）を返す。
    [[nodiscard]] float gain() const noexcept {
        return gain_;
    }

private:
    float release_coefficient_;
    float gain_ = 1.0F;
};

}  // namespace vocroz::audio_core
