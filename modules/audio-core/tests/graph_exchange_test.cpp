// SPDX-License-Identifier: AGPL-3.0-only
//
// GraphExchange の単体テスト。グラフの差し替え、古いグラフの退役と破棄、
// オーディオスレッドが取り出す前の置き換えを検証する。

#include "graph_exchange.h"

#include <atomic>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "vocroz/audio_core/constants.h"

namespace vocroz::audio_core {
namespace {

// 破棄されたことを数える AudioSource。グラフの破棄でソースの参照が外れたかを確かめる。
class CountingSource final : public AudioSource {
public:
    explicit CountingSource(std::atomic<int>& destroyed) : destroyed_(destroyed) {}
    ~CountingSource() override {
        destroyed_.fetch_add(1);
    }
    void pull(std::span<float> out) noexcept VOCROZ_NONBLOCKING override {
        std::fill(out.begin(), out.end(), 0.0F);
    }

private:
    std::atomic<int>& destroyed_;
};

std::unique_ptr<RenderGraph> graph_with_source(std::atomic<int>& destroyed) {
    GraphDescription description;
    description.nodes = {
        NodeSpec::make_source(NodeId{1}, std::make_shared<CountingSource>(destroyed))};
    return std::move(RenderGraph::build(description, 0, 0)).value();
}

TEST(GraphExchangeTest, ReturnsNullUntilFirstGraphIsPublished) {
    GraphExchange exchange;
    EXPECT_EQ(exchange.acquire(), nullptr);
    EXPECT_TRUE(exchange.is_settled());
}

TEST(GraphExchangeTest, SwapsAtAcquireAndDestroysOldGraphOnCollect) {
    std::atomic<int> destroyed{0};
    GraphExchange exchange;
    auto first = graph_with_source(destroyed);
    RenderGraph* first_raw = first.get();
    exchange.publish(std::move(first));
    EXPECT_FALSE(exchange.is_settled());
    EXPECT_EQ(exchange.acquire(), first_raw);

    auto second = graph_with_source(destroyed);
    RenderGraph* second_raw = second.get();
    exchange.publish(std::move(second));
    // オーディオスレッドが取り込むまでは古いグラフを使い続ける。
    EXPECT_EQ(destroyed.load(), 0);
    EXPECT_EQ(exchange.acquire(), second_raw);
    // オーディオスレッドは古いグラフを破棄せず、コントロールスレッドの collect() で破棄する。
    EXPECT_EQ(destroyed.load(), 0);
    EXPECT_TRUE(exchange.is_settled());
    EXPECT_EQ(destroyed.load(), 1);
}

TEST(GraphExchangeTest, DestroysUnusedPendingGraphWhenReplaced) {
    std::atomic<int> destroyed{0};
    GraphExchange exchange;
    exchange.publish(graph_with_source(destroyed));
    exchange.publish(graph_with_source(destroyed));
    // 一度も使われなかった最初のグラフは、置き換えた時点で破棄する。
    EXPECT_EQ(destroyed.load(), 1);
}

TEST(GraphExchangeTest, DefersSwapWhileRetiredGraphIsUncollected) {
    std::atomic<int> destroyed{0};
    GraphExchange exchange;
    exchange.publish(graph_with_source(destroyed));
    RenderGraph* first = exchange.acquire();
    exchange.publish(graph_with_source(destroyed));
    RenderGraph* second = exchange.acquire();  // first を退役させる
    ASSERT_NE(first, second);

    // collect() を挟まずに次を渡すと、publish() が退役済みを先に破棄してから置く。
    exchange.publish(graph_with_source(destroyed));
    EXPECT_EQ(destroyed.load(), 1);
    RenderGraph* third = exchange.acquire();
    EXPECT_NE(third, second);
}

TEST(GraphExchangeTest, SurvivesConcurrentPublishAndAcquire) {
    std::atomic<int> destroyed{0};
    constexpr int kPublishes = 2000;
    {
        GraphExchange exchange;
        std::atomic<bool> done{false};
        std::thread audio([&] {
            FaderBank faders(0, 1);
            MeterBank meters(0);
            while (!done.load()) {
                // 取り込んだグラフを実際に処理し、破棄済みのグラフに触れていないことを確かめる。
                if (RenderGraph* graph = exchange.acquire()) {
                    (void)graph->process(kBlockSize, faders, meters);
                }
            }
        });
        for (int i = 0; i < kPublishes; ++i) {
            exchange.publish(graph_with_source(destroyed));
        }
        done.store(true);
        audio.join();
    }
    // 途中で使われなかったものも、最後に使っていたものも、すべて 1 回ずつ破棄される。
    EXPECT_EQ(destroyed.load(), kPublishes);
}

}  // namespace
}  // namespace vocroz::audio_core
