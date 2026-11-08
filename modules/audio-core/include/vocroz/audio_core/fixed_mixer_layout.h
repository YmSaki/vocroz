// SPDX-License-Identifier: AGPL-3.0-only
//
// Alpha の固定ミキサー（モノラル入力チャンネル N 本 + Master）を、音声グラフの 1
// 構成として記述する。 グラフ: ソース_i → ストリップ_i → Master バス → 安全リミッター → 出力 Beta1
// のルーティングは同じ GraphDescription で別の構成を記述するので、ミキサーを作り直さずに済む。
#pragma once

#include <cstddef>
#include <memory>
#include <span>

#include "vocroz/audio_core/graph_description.h"
#include "vocroz/audio_core/ports/audio_source.h"

namespace vocroz::audio_core {

/// 固定ミキサーのフェーダー、メーター、ノードの割り当て。
/// フェーダーとメーターの識別子は、チャンネル i が i、Master が channel_count()。
/// スレッド安全性: 不変の値型。どのスレッドからも呼び出し可能。
class FixedMixerLayout {
public:
    /// @param channel_count モノラル入力チャンネルの数（1 以上。Alpha は 8 本以上を保証する）
    explicit FixedMixerLayout(std::size_t channel_count) noexcept;

    /// 入力チャンネルの数を返す。
    [[nodiscard]] std::size_t channel_count() const noexcept {
        return channel_count_;
    }

    /// 入力チャンネルのフェーダーを返す。
    /// @param channel チャンネルの番号（0 から channel_count() 未満）
    [[nodiscard]] FaderId channel_fader(std::size_t channel) const noexcept;

    /// 入力チャンネルのプリフェーダーのメーターを返す。
    /// @param channel チャンネルの番号（0 から channel_count() 未満）
    [[nodiscard]] MeterId channel_meter(std::size_t channel) const noexcept;

    /// Master のフェーダーを返す。
    [[nodiscard]] FaderId master_fader() const noexcept;

    /// Master の最終出力（リミッターの後）のメーターを返す。
    [[nodiscard]] MeterId master_meter() const noexcept;

    /// 必要なフェーダーとメーターの数（MixerConfig の容量の下限）を返す。
    [[nodiscard]] std::size_t required_capacity() const noexcept {
        return channel_count_ + 1;
    }

    /// グラフを記述する。
    /// @param sources チャンネルごとのソース。要素が null のチャンネルと、sources より後ろの
    ///                チャンネルは、ストリップだけを置いて無音にする（参加者がいないチャンネル）
    /// @return グラフの記述
    [[nodiscard]] GraphDescription describe(
        std::span<const std::shared_ptr<AudioSource>> sources) const;

private:
    std::size_t channel_count_;
};

}  // namespace vocroz::audio_core
