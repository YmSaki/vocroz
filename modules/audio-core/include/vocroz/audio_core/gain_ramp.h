// SPDX-License-Identifier: AGPL-3.0-only
//
// ゲインの変化をサンプル単位で直線的に補間するランプ。
// 音量やミュートを 1 サンプルで切り替えると波形が不連続になりクリックノイズが出るので、
// フェーダー、ミュート、子機の送信ゲインの変化はすべて 5〜20ms のランプで適用する。
#pragma once

#include <cstddef>

#include "vocroz/audio_core/realtime.h"

namespace vocroz::audio_core {

/// リニアゲインのランプ。初期値は 0（無音）で、初期ミュートの状態から始まる。
/// スレッド安全性: 1 つのインスタンスを 1 つのスレッド（通常はオーディオスレッド）だけが使う。
/// すべてのメンバー関数はヒープを確保せず、オーディオスレッドから呼び出し可能。
class GainRamp {
public:
    /// 指定した長さのランプを作る。
    /// @param ramp_samples 現在値から目標値まで変化させるサンプル数（1 以上。0 は 1 として扱う）
    explicit GainRamp(std::size_t ramp_samples) noexcept;

    /// 目標値を設定し、現在値から ramp_samples かけて変化させる。
    /// ランプの途中で呼んだ場合は、その時点の値から新しい目標値へ向かう。
    /// @param target 目標のリニアゲイン
    void set_target(float target) noexcept VOCROZ_NONBLOCKING;

    /// ランプを使わずに、現在値と目標値を value にする。
    /// 出力デバイスの消失など、クリックノイズより即時性を優先する場合に使う。
    /// @param value 設定するリニアゲイン
    void jump_to(float value) noexcept VOCROZ_NONBLOCKING;

    /// 次の 1 サンプルに掛けるゲインを返し、ランプを 1 サンプル進める。
    [[nodiscard]] float next() noexcept VOCROZ_NONBLOCKING;

    /// planar 形式のバッファにゲインを掛け、ランプを frames サンプル進める。
    /// @param channels 各チャンネルの先頭へのポインターの配列。各チャンネルは frames サンプル以上
    /// @param channel_count チャンネル数
    /// @param frames 処理するサンプル数
    void apply(float* const* channels, std::size_t channel_count,
               std::size_t frames) noexcept VOCROZ_NONBLOCKING;

    /// 現在値（次のサンプルに掛けるゲイン）を返す。
    [[nodiscard]] float current() const noexcept {
        return current_;
    }

    /// 目標値を返す。
    [[nodiscard]] float target() const noexcept {
        return target_;
    }

    /// 目標値に到達していれば true を返す。
    [[nodiscard]] bool is_settled() const noexcept {
        return remaining_ == 0;
    }

private:
    std::size_t ramp_samples_;
    float current_ = 0.0F;
    float target_ = 0.0F;
    float step_ = 0.0F;
    std::size_t remaining_ = 0;
};

}  // namespace vocroz::audio_core
