// SPDX-License-Identifier: AGPL-3.0-only
//
// FaderBank の実装。

#include "fader_bank.h"

#include <algorithm>

namespace vocroz::audio_core {

FaderBank::FaderBank(std::size_t capacity, std::size_t ramp_samples)
    : faders_(capacity, Fader{GainRamp(ramp_samples), 1.0F, true, GainRamp(ramp_samples), 0}) {}

void FaderBank::apply(const ParameterChange& change) noexcept VOCROZ_NONBLOCKING {
    if (change.fader.value >= faders_.size()) {
        return;
    }
    Fader& fader = faders_[change.fader.value];
    switch (change.kind) {
        case ParameterChange::Kind::kGain:
            fader.gain = change.gain;
            break;
        case ParameterChange::Kind::kMute:
            fader.muted = change.muted;
            break;
        case ParameterChange::Kind::kReset:
            fader.gain = 1.0F;
            fader.muted = true;
            // 前の参加者の音を新しい参加者のストリップに残さないよう、ランプを使わずに無音にする。
            fader.ramp.jump_to(0.0F);
            return;
    }
    // ミュートはゲインを 0 にするランプとして扱い、ミュートと解除でもクリックノイズを出さない。
    fader.ramp.set_target(fader.muted ? 0.0F : fader.gain);
}

void FaderBank::process(FaderId fader, float* const* channels, std::size_t channel_count,
                        std::size_t frames) noexcept VOCROZ_NONBLOCKING {
    if (fader.value >= faders_.size()) {
        // グラフの構築時に範囲を検証しているので通常は起きない。起きたら無音に倒す。
        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            std::fill(channels[ch], channels[ch] + frames, 0.0F);
        }
        return;
    }
    Fader& state = faders_[fader.value];
    // follow() のために、このブロックで進める前の状態を残す。変更（apply()）はブロックの間にしか
    // 来ないので、ここで写した状態から process() と follow() が同じゲインの列を作る。
    if (state.block_start_of != block_) {
        state.block_start = state.ramp;
        state.block_start_of = block_;
    }
    state.ramp.apply(channels, channel_count, frames);
}

void FaderBank::begin_block() noexcept VOCROZ_NONBLOCKING {
    ++block_;
}

void FaderBank::follow(FaderId fader, float* const* channels, std::size_t channel_count,
                       std::size_t frames) const noexcept VOCROZ_NONBLOCKING {
    if (fader.value >= faders_.size()) {
        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            std::fill(channels[ch], channels[ch] + frames, 0.0F);
        }
        return;
    }
    // このブロックで process() 済みなら進める前の状態から、まだならこれから進める状態から掛ける。
    // 写しを進めるので、元のフェーダーの状態は変わらない。
    const Fader& state = faders_[fader.value];
    GainRamp ramp = state.block_start_of == block_ ? state.block_start : state.ramp;
    ramp.apply(channels, channel_count, frames);
}

}  // namespace vocroz::audio_core
