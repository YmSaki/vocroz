// SPDX-License-Identifier: AGPL-3.0-only
//
// すべてのレベルメーターの状態。
// オーディオスレッドがピークと RMS を算出してアトミック変数に書き、
// コントロールスレッドが UI の更新周期（10〜30 Hz）で読む。ロックを使わない。
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

#include "vocroz/audio_core/graph_description.h"
#include "vocroz/audio_core/meter_reading.h"
#include "vocroz/audio_core/realtime.h"

namespace vocroz::audio_core {

/// MeterId で引くメーターの集まり。
/// スレッド安全性: measure() はオーディオスレッドだけ、read() はコントロールスレッドだけが呼ぶ。
/// 構築と破棄はオーディオスレッド外で行う。
class MeterBank {
public:
    /// メーターを事前に確保する。
    /// @param capacity メーターの数
    explicit MeterBank(std::size_t capacity);

    /// planar 形式のバッファのレベルを測り、メーターに反映する。範囲外の MeterId は無視する。
    /// @param meter 対象のメーター
    /// @param channels 各チャンネルの先頭へのポインターの配列
    /// @param channel_count チャンネル数
    /// @param frames 測るサンプル数
    void measure(MeterId meter, const float* const* channels, std::size_t channel_count,
                 std::size_t frames) noexcept VOCROZ_NONBLOCKING;

    /// メーターの値を読む。ピークは読むたびにリセットする。範囲外の MeterId は 0 を返す。
    /// @param meter 対象のメーター
    /// @return ピークと RMS
    [[nodiscard]] MeterReading read(MeterId meter) noexcept;

    /// メーターの数を返す。
    [[nodiscard]] std::size_t capacity() const noexcept {
        return capacity_;
    }

private:
    static_assert(std::atomic<float>::is_always_lock_free,
                  "オーディオスレッドでロックを取らないよう、メーターの値はロックフリーに限る");

    struct Slot {
        std::atomic<float> peak_hold{0.0F};    // 前回の読み取りからの最大値
        std::atomic<float> mean_square{0.0F};  // 平滑化した二乗平均（公開用）
        float mean_square_state = 0.0F;  // オーディオスレッドだけが使う平滑化の状態
    };

    std::size_t capacity_;
    std::unique_ptr<Slot[]> slots_;  // アトミック変数はムーブできないので、配列で確保する
};

}  // namespace vocroz::audio_core
