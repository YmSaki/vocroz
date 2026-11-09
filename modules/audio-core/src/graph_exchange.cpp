// SPDX-License-Identifier: AGPL-3.0-only
//
// GraphExchange の実装。

#include "graph_exchange.h"

namespace vocroz::audio_core {
namespace {

// unique_ptr から release した所有権を持つポインターを破棄する。
void destroy(RenderGraph* graph) noexcept {
    std::unique_ptr<RenderGraph> owner(graph);
}

}  // namespace

GraphExchange::~GraphExchange() {
    destroy(pending_.exchange(nullptr));
    destroy(retired_.exchange(nullptr));
    destroy(active_);
}

void GraphExchange::publish(std::unique_ptr<RenderGraph> graph) {
    collect();
    // オーディオスレッドが取り出す前に置き換えた前回のグラフは、一度も使われていないので
    // ここで破棄してよい（exchange が返したものは、オーディオスレッドの手に渡っていない）。
    destroy(pending_.exchange(graph.release(), std::memory_order_acq_rel));
}

void GraphExchange::collect() noexcept {
    destroy(retired_.exchange(nullptr, std::memory_order_acquire));
}

bool GraphExchange::is_settled() noexcept {
    collect();
    return pending_.load(std::memory_order_acquire) == nullptr &&
           retired_.load(std::memory_order_acquire) == nullptr;
}

RenderGraph* GraphExchange::acquire() noexcept VOCROZ_NONBLOCKING {
    // 退役させたグラフが残っている間は取り込まない。退役の置き場が 1 つなので、
    // 先に取り込むと置き場がなくなり、オーディオスレッドで破棄せざるを得なくなるため。
    if (retired_.load(std::memory_order_acquire) == nullptr) {
        RenderGraph* incoming = pending_.exchange(nullptr, std::memory_order_acq_rel);
        if (incoming != nullptr) {
            // 退役させる前に発音中のノートを引き継ぐ。退役させた後はコントロールスレッドが
            // いつ破棄してもよいので、ここでしか読めない。
            if (active_ != nullptr) {
                incoming->adopt_midi_state(*active_);
            }
            retired_.store(active_, std::memory_order_release);
            active_ = incoming;
        }
    }
    return active_;
}

}  // namespace vocroz::audio_core
