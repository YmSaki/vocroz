// SPDX-License-Identifier: AGPL-3.0-only
//
// コントロールスレッドからオーディオスレッドへ SPSC キューで渡すフェーダーの変更。
#pragma once

#include "vocroz/audio_core/graph_description.h"

namespace vocroz::audio_core {

/// フェーダーへの 1 回の変更。SPSC キューの要素にするため、トリビアルにコピー可能な型にする。
struct ParameterChange {
    /// 変更の種類。
    enum class Kind {
        kGain,   ///< ゲインを gain（リニア）にする
        kMute,   ///< ミュートを muted にする
        kReset,  ///< ミュートかつ 0 dB にし、ランプを使わずに無音にする（新規参加者のストリップ）
    };

    Kind kind = Kind::kReset;  ///< 変更の種類
    FaderId fader;             ///< 対象のフェーダー
    float gain = 0.0F;         ///< kGain のときのリニアゲイン
    bool muted = true;         ///< kMute のときのミュート
};

}  // namespace vocroz::audio_core
