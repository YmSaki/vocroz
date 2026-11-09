// SPDX-License-Identifier: AGPL-3.0-only
//
// 音声グラフの返送タップ（NodeKind::kReturnTap）がサンプルを渡す先のポート。
// 親機では transport の MediaStreamEncoder（子機への返送のエンコード）が実装する
// （docs/adr/0036-return-monitor.md）。
// オーディオスレッドから押し込む（push）形にするのは、エンコードをデバイスの周期の中で済ませ、
// 返送の経路に追加のバッファリングを入れないため（低レイテンシ）。
#pragma once

#include <span>

#include "vocroz/audio_core/realtime.h"

namespace vocroz::audio_core {

/// モノラルの音声の受け取り先。
/// グラフ（GraphDescription）に std::shared_ptr で渡す。グラフを差し替えた後、古いグラフは
/// オーディオスレッド外で破棄されるので、受け取り先の最後の参照もオーディオスレッド外で外れる。
class AudioSink {
public:
    virtual ~AudioSink() = default;

    /// 1 ブロックぶんのサンプルを受け取る。ブロックは途切れずに順に渡す。
    /// @param samples モノラルのサンプル（kBlockSize 以下）。サンプルレートは kSampleRate。
    ///                呼び出しの間だけ有効
    /// エラー時の振る舞い: 例外を送出しない。受け取れないサンプルは実装が捨てる。
    /// スレッド安全性: オーディオスレッドから呼び出し可能。実装は第 9 節のリアルタイム制約に従う。
    virtual void push(std::span<const float> samples) noexcept VOCROZ_NONBLOCKING = 0;
};

}  // namespace vocroz::audio_core
