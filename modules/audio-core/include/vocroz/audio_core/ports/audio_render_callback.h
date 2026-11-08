// SPDX-License-Identifier: AGPL-3.0-only
//
// 出力デバイスのコールバックから呼ばれ、出力するサンプルを書き込むリスナー。
// AudioOutput の実装（device）がオーディオスレッドで呼び、Mixer が実装する。
#pragma once

#include "vocroz/audio_core/audio_buffer.h"
#include "vocroz/audio_core/realtime.h"

namespace vocroz::audio_core {

/// 出力するサンプルを生成するリスナー。
class AudioRenderCallback {
public:
    virtual ~AudioRenderCallback() = default;

    /// 出力するサンプルを out にすべて書き込む。
    /// @param out 書き込み先。サンプルレートは
    /// kSampleRate。フレーム数はコールバックごとに変わってよい エラー時の振る舞い:
    /// 例外を送出しない。生成できないサンプルは無音（0）で埋める。 スレッド安全性:
    /// オーディオスレッドから呼び出し可能。実装は第 9 節のリアルタイム制約に従う。
    virtual void render(InterleavedBuffer out) noexcept VOCROZ_NONBLOCKING = 0;
};

}  // namespace vocroz::audio_core
