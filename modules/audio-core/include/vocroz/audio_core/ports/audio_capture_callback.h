// SPDX-License-Identifier: AGPL-3.0-only
//
// 入力デバイスのコールバックから呼ばれ、キャプチャしたサンプルを受け取るリスナー。
// AudioInput の実装（device、capture）がオーディオスレッドで呼ぶ。
#pragma once

#include "vocroz/audio_core/audio_buffer.h"
#include "vocroz/audio_core/realtime.h"

namespace vocroz::audio_core {

/// キャプチャしたサンプルを受け取るリスナー。
class AudioCaptureCallback {
public:
    virtual ~AudioCaptureCallback() = default;

    /// キャプチャしたサンプルを受け取る。
    /// @param in キャプチャしたサンプル。サンプルレートは kSampleRate。呼び出しの間だけ有効
    /// エラー時の振る舞い: 例外を送出しない。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。実装は第 9 節のリアルタイム制約に従う。
    virtual void on_captured(ConstInterleavedBuffer in) noexcept VOCROZ_NONBLOCKING = 0;
};

}  // namespace vocroz::audio_core
