// SPDX-License-Identifier: AGPL-3.0-only
//
// 音声グラフのストリームタップ（NodeKind::kStreamTap）がステレオのサンプルを渡す先のポート。
// 親機では transport の MediaStreamEncoder（SRTP 外部出力のエンコード）が実装する
// （docs/adr/0040-srtp-external-output.md）。
// 返送タップの AudioSink（モノラル）と分けるのは、外部出力は放送機器へ送るのでステレオのまま
// 渡す必要があるため。オーディオスレッドから押し込む形にするのは、AudioSink と同じく
// エンコードをデバイスの周期の中で済ませ、追加のバッファリングを入れないため（低レイテンシ）。
#pragma once

#include <span>

#include "vocroz/audio_core/realtime.h"

namespace vocroz::audio_core {

/// ステレオの音声の受け取り先。
/// グラフ（GraphDescription）に std::shared_ptr で渡す。古いグラフはオーディオスレッド外で
/// 破棄されるので、受け取り先の最後の参照もオーディオスレッド外で外れる。
class AudioStreamSink {
public:
    virtual ~AudioStreamSink() = default;

    /// 1 ブロックぶんのステレオのサンプルを受け取る。ブロックは途切れずに順に渡す。
    /// @param left  L チャンネルのサンプル（kBlockSize 以下）。サンプルレートは kSampleRate。
    ///              呼び出しの間だけ有効
    /// @param right R チャンネルのサンプル。left と同じ長さ
    /// エラー時の振る舞い: 例外を送出しない。受け取れないサンプルは実装が捨てる。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。実装は第 9 節のリアルタイム制約に従う。
    virtual void push_stereo(std::span<const float> left,
                             std::span<const float> right) noexcept VOCROZ_NONBLOCKING = 0;
};

}  // namespace vocroz::audio_core
