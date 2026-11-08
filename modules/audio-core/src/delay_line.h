// SPDX-License-Identifier: AGPL-3.0-only
//
// 固定長のディレイライン（リングバッファ）。入力ストリップの手動同期オフセットと、
// フィードバック経路を許す遅延ノードの両方で、サンプルを決まった数だけ遅らせる知識として使う。
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "vocroz/audio_core/realtime.h"

namespace vocroz::audio_core {

/// サンプルを delay_samples だけ遅らせるディレイライン。最初は無音で満たされている。
/// スレッド安全性: 構築と破棄はオーディオスレッド外で行う。それ以外のメンバー関数は
/// オーディオスレッドだけが呼ぶ。
class DelayLine {
public:
    /// バッファを事前に確保する。
    /// @param delay_samples 遅延（サンプル）。0 なら遅らせない
    explicit DelayLine(std::size_t delay_samples);

    /// 遅延を返す。
    [[nodiscard]] std::size_t delay() const noexcept {
        return buffer_.size();
    }

    /// samples をその場で遅らせる。遅延がブロック長より短くても長くてもよい。
    /// @param samples 入力。delay() サンプル前の入力で置き換える
    void process(std::span<float> samples) noexcept VOCROZ_NONBLOCKING;

    /// 遅延したサンプルを読む。write() と組で使い、read() の後に同じ数の write() を呼ぶ。
    /// 読む数は delay() 以下にする（超えたぶんはまだ書いていない時刻のサンプルなので無音にする）。
    /// フィードバック経路の遅延ノードは、このブロックの入力が決まる前に出力を作るので、
    /// process() ではなくこの 2 段階で使う。
    /// @param out 書き込み先
    void read(std::span<float> out) const noexcept VOCROZ_NONBLOCKING;

    /// read() で読んだ位置に入力を書き、読み書きの位置を進める。
    /// @param in 入力（read() で読んだ数と同じ長さ）
    void write(std::span<const float> in) noexcept VOCROZ_NONBLOCKING;

private:
    std::vector<float> buffer_;
    std::size_t position_ = 0;  // 次に読み、書く位置（最も古いサンプル）
};

}  // namespace vocroz::audio_core
