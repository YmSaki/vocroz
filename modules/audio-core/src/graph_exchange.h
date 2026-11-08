// SPDX-License-Identifier: AGPL-3.0-only
//
// レンダーグラフをオーディオスレッドへ渡し、古いグラフをオーディオスレッド外で破棄するための受け渡し口
// （ダブルバッファリングによるグラフのスワップ、AGENTS.md 6.1）。
//
// オーディオスレッドは処理中のグラフ（active_）を自分だけで持つ。コントロールスレッドは新しいグラフを
// pending_ に置き、オーディオスレッドはコールバックの先頭でそれを取り出して active_ と入れ替え、
// 古いグラフを retired_ に置く。古いグラフの破棄（メモリの解放と AudioSource の参照の解放）は、
// コントロールスレッドが retired_
// から取り出して行うので、オーディオスレッドではヒープを解放しない。
#pragma once

#include <atomic>
#include <memory>

#include "vocroz/audio_core/realtime.h"

#include "render_graph.h"

namespace vocroz::audio_core {

/// RenderGraph の受け渡し口。
/// スレッド安全性: publish()、collect()、is_settled() はコントロールスレッド（1 つ）だけ、
/// acquire() はオーディオスレッドだけが呼ぶ。破棄はオーディオスレッドが止まってから行う。
class GraphExchange {
public:
    GraphExchange() = default;
    GraphExchange(const GraphExchange&) = delete;
    GraphExchange& operator=(const GraphExchange&) = delete;
    GraphExchange(GraphExchange&&) = delete;
    GraphExchange& operator=(GraphExchange&&) = delete;
    ~GraphExchange();

    /// 新しいグラフをオーディオスレッドへ渡す。オーディオスレッドがまだ取り出していない前回の
    /// グラフがあれば、それは使われないまま破棄する。退役済みのグラフもここで破棄する。
    /// @param graph 渡すグラフ（null にしない。無音にするときは空の記述から構築したグラフを渡す）
    void publish(std::unique_ptr<RenderGraph> graph);

    /// オーディオスレッドが退役させたグラフを破棄する。
    void collect() noexcept;

    /// 渡したグラフをオーディオスレッドが使い始め、古いグラフを破棄し終えていれば true を返す。
    /// AudioSource をグラフから外した後、その AudioSource の後始末を始めてよいかの判定に使う。
    [[nodiscard]] bool is_settled() noexcept;

    /// 新しいグラフがあれば取り込み、処理に使うグラフを返す。
    /// 前回退役させたグラフをコントロールスレッドがまだ破棄していなければ、取り込みを次回に延ばす。
    /// @return 処理に使うグラフ。グラフがなければ nullptr
    [[nodiscard]] RenderGraph* acquire() noexcept VOCROZ_NONBLOCKING;

private:
    std::atomic<RenderGraph*> pending_{
        nullptr};  // コントロールスレッドが置き、オーディオスレッドが取る
    std::atomic<RenderGraph*> retired_{
        nullptr};  // オーディオスレッドが置き、コントロールスレッドが取る
    RenderGraph* active_ = nullptr;  // オーディオスレッドだけが触る
};

}  // namespace vocroz::audio_core
