// SPDX-License-Identifier: AGPL-3.0-only
//
// すべてのフェーダー（ゲイン、ミュート、ゲインランプ）の状態。
// グラフをスワップしてもフェーダーの値とランプの途中経過が続くよう、状態をグラフの外に置く。
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "vocroz/audio_core/gain_ramp.h"
#include "vocroz/audio_core/graph_description.h"
#include "vocroz/audio_core/realtime.h"

#include "parameter_change.h"

namespace vocroz::audio_core {

/// FaderId で引くフェーダーの集まり。すべてのフェーダーはミュートで始まる（初期ミュート）。
/// スレッド安全性: 構築と破棄はオーディオスレッド外で行う。それ以外のメンバー関数は
/// オーディオスレッドだけが呼ぶ。
class FaderBank {
public:
    /// フェーダーを事前に確保する。
    /// @param capacity フェーダーの数
    /// @param ramp_samples ゲインランプの長さ（サンプル）
    FaderBank(std::size_t capacity, std::size_t ramp_samples);

    /// 変更を適用する。範囲外の FaderId は無視する。
    /// @param change 適用する変更
    void apply(const ParameterChange& change) noexcept VOCROZ_NONBLOCKING;

    /// フェーダーのゲインを planar 形式のバッファに掛ける。範囲外の FaderId なら無音にする。
    /// @param fader 対象のフェーダー
    /// @param channels 各チャンネルの先頭へのポインターの配列
    /// @param channel_count チャンネル数
    /// @param frames 処理するサンプル数
    void process(FaderId fader, float* const* channels, std::size_t channel_count,
                 std::size_t frames) noexcept VOCROZ_NONBLOCKING;

    /// 処理ブロックの区切りを知らせる。RenderGraph::process() がブロックの先頭で呼ぶ。
    /// follow() が、同じブロックで process() が進める前のランプの状態を使えるようにするため。
    void begin_block() noexcept VOCROZ_NONBLOCKING;

    /// フェーダーのゲインを、ランプを進めずに掛ける。同じブロックで同じフェーダーを process()
    /// する結線と、処理順に関わらずサンプル単位で同じゲインになる。
    /// 子機への返送（Minus-Me）が、親機のミックスのセンドと同じミュートと音量で取るために使う
    /// （docs/adr/0036-return-monitor.md）。範囲外の FaderId なら無音にする。
    /// @param fader 対象のフェーダー
    /// @param channels 各チャンネルの先頭へのポインターの配列
    /// @param channel_count チャンネル数
    /// @param frames 処理するサンプル数
    void follow(FaderId fader, float* const* channels, std::size_t channel_count,
                std::size_t frames) const noexcept VOCROZ_NONBLOCKING;

    /// フェーダーの数を返す。
    [[nodiscard]] std::size_t capacity() const noexcept {
        return faders_.size();
    }

private:
    struct Fader {
        GainRamp ramp;
        float gain = 1.0F;  // ミュート解除時に戻るリニアゲイン（0 dB）
        bool muted = true;  // 初期ミュート
        // block_start_of の時点（そのブロックで最初に process() する前）のランプ。
        GainRamp block_start;
        std::uint64_t block_start_of = 0;  // block_start を写したブロックの番号
    };

    std::vector<Fader> faders_;
    // 処理ブロックの番号。1 から数え、Fader::block_start_of の初期値 0 と区別する。
    std::uint64_t block_ = 1;
};

}  // namespace vocroz::audio_core
