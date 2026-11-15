// SPDX-License-Identifier: AGPL-3.0-only
//
// GraphExchange の単体テスト。グラフの差し替え、古いグラフの退役と破棄、
// オーディオスレッドが取り出す前の置き換えを検証する。差し替えで音声の履歴（手動ディレイ、
// 遅延ノード、遅延補正）を HistoryKey で引き継ぐことも検証する
// （docs/adr/0045-audio-history-carry-over.md）。

#include "graph_exchange.h"

#include <atomic>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/audio_core/constants.h"

#include "fakes/fake_audio_source.h"
#include "fakes/fake_plugin_processor.h"

namespace vocroz::audio_core {
namespace {

// 破棄されたことを数える AudioSource。グラフの破棄でソースの参照が外れたかを確かめる。
class CountingSource final : public AudioSource {
public:
    explicit CountingSource(std::atomic<int>& destroyed) : destroyed_(destroyed) {}
    ~CountingSource() override {
        destroyed_.fetch_add(1);
    }
    [[nodiscard]] std::size_t channel_count() const noexcept override {
        return 1;
    }
    void pull(float* const* channels, std::size_t frames) noexcept VOCROZ_NONBLOCKING override {
        std::fill(channels[0], channels[0] + frames, 0.0F);
    }

private:
    std::atomic<int>& destroyed_;
};

// サンプルの位置 n に n + 1 を出すソース。出力の値から、どの時刻の入力かが分かる。
class IndexSource final : public AudioSource {
public:
    [[nodiscard]] std::size_t channel_count() const noexcept override {
        return 1;
    }
    void pull(float* const* channels, std::size_t frames) noexcept VOCROZ_NONBLOCKING override {
        for (std::size_t i = 0; i < frames; ++i) {
            channels[0][i] = static_cast<float>(++position_);
        }
    }

private:
    std::size_t position_ = 0;
};

constexpr std::size_t kCapacity = 8;
constexpr HistoryKey kStripKey{1};
constexpr HistoryKey kDelayKey{2};
constexpr HistoryKey kBusKey{3};
constexpr HistoryKey kOtherStripKey{4};

void unmute(FaderBank& faders, std::initializer_list<std::uint16_t> ids) {
    for (const std::uint16_t id : ids) {
        ParameterChange change;
        change.kind = ParameterChange::Kind::kMute;
        change.fader = FaderId{id};
        change.muted = false;
        faders.apply(change);
    }
}

NodeSpec keyed(NodeSpec spec, std::optional<HistoryKey> key) {
    spec.history_key = key;
    return spec;
}

// ソース 1 → ストリップ 2（手動ディレイ）→ 出力 3。extra_strip なら、入力のないストリップ 4 を
// 足す（別の参加者のストリップを追加した状態）。
GraphDescription delayed_strip(std::shared_ptr<AudioSource> source, std::size_t delay,
                               std::optional<HistoryKey> key, bool extra_strip = false) {
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_source(NodeId{1}, std::move(source)),
                   keyed(NodeSpec::make_strip(NodeId{2}, FaderId{0}, std::nullopt, delay), key),
                   NodeSpec::make_output(NodeId{3}, std::nullopt)};
    graph.connections = {{NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}}};
    if (extra_strip) {
        graph.nodes.push_back(keyed(
            NodeSpec::make_strip(NodeId{4}, FaderId{1}, std::nullopt, delay), kOtherStripKey));
        graph.connections.emplace_back(NodeId{4}, NodeId{3});
    }
    return graph;
}

std::unique_ptr<RenderGraph> build(const GraphDescription& description) {
    return std::move(RenderGraph::build(description, kCapacity, kCapacity)).value();
}

// 差し替えの前後でグラフを処理し、出力の L を順に返す。
class HistoryCarryOverTest : public ::testing::Test {
protected:
    HistoryCarryOverTest() {
        unmute(faders_, {0, 1, 2, 3});
    }

    std::vector<float> render(std::size_t blocks) {
        std::vector<float> left;
        for (std::size_t b = 0; b < blocks; ++b) {
            RenderGraph* graph = exchange_.acquire();
            const auto out = graph->process(kBlockSize, faders_, meters_);
            left.insert(left.end(), out[0].left, out[0].left + kBlockSize);
            exchange_.collect();
        }
        return left;
    }

    FaderBank faders_{kCapacity, 1};
    MeterBank meters_{kCapacity};
    GraphExchange exchange_;
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

TEST_F(HistoryCarryOverTest, AddingAnotherStripKeepsDelayedSamplesOfExistingStrip) {
    // ブロック長より長い遅延。差し替えの後の 1 ブロックは、すべて差し替え前に入った音になる。
    constexpr std::size_t kDelay = 300;
    const auto source = std::make_shared<IndexSource>();
    exchange_.publish(build(delayed_strip(source, kDelay, kStripKey)));
    const std::vector<float> before = render(4);
    exchange_.publish(build(delayed_strip(source, kDelay, kStripKey, true)));
    const std::vector<float> after = render(3);

    // 出力の位置 n は、入力の位置 n - kDelay（値は n - kDelay + 1）。差し替えをまたいで途切れない。
    // 先頭はフェーダーのランプ（1 サンプル）なので、遅延が満たされた後から比べる。
    std::vector<float> all = before;
    all.insert(all.end(), after.begin(), after.end());
    for (std::size_t n = kDelay + 1; n < all.size(); ++n) {
        ASSERT_FLOAT_EQ(all[n], static_cast<float>(n - kDelay + 1)) << n;
    }
}

TEST_F(HistoryCarryOverTest, ChangedDelayStartsFromSilence) {
    constexpr std::size_t kDelay = 300;
    constexpr std::size_t kChanged = 200;
    const auto source = std::make_shared<IndexSource>();
    exchange_.publish(build(delayed_strip(source, kDelay, kStripKey)));
    (void)render(4);
    exchange_.publish(build(delayed_strip(source, kChanged, kStripKey)));
    const std::vector<float> after = render(2);
    // 遅延の長さを変えたのはオペレーターなので、前の長さの音を出さずに無音から始める。
    for (std::size_t n = 0; n < kChanged; ++n) {
        ASSERT_EQ(after[n], 0.0F) << n;
    }
    EXPECT_GT(after[kChanged], 0.0F);
}

TEST_F(HistoryCarryOverTest, StripWithoutHistoryKeyStartsFromSilence) {
    constexpr std::size_t kDelay = 300;
    const auto source = std::make_shared<IndexSource>();
    exchange_.publish(build(delayed_strip(source, kDelay, std::nullopt)));
    (void)render(4);
    exchange_.publish(build(delayed_strip(source, kDelay, std::nullopt, true)));
    const std::vector<float> after = render(3);
    for (std::size_t n = 0; n < kDelay; ++n) {
        ASSERT_EQ(after[n], 0.0F) << n;
    }
}

TEST_F(HistoryCarryOverTest, DuplicatedHistoryKeyIsNotCarried) {
    constexpr std::size_t kDelay = 300;
    const auto source = std::make_shared<IndexSource>();
    exchange_.publish(build(delayed_strip(source, kDelay, kStripKey)));
    (void)render(4);
    // 追加したストリップにも同じキーを付けると、どちらが元のストリップか決められない。
    GraphDescription next = delayed_strip(source, kDelay, kStripKey, true);
    next.nodes.back().history_key = kStripKey;
    exchange_.publish(build(next));
    const std::vector<float> after = render(3);
    for (std::size_t n = 0; n < kDelay; ++n) {
        ASSERT_EQ(after[n], 0.0F) << n;
    }
}

TEST_F(HistoryCarryOverTest, CarriesDelayNodeHistory) {
    // ソース 1 → ストリップ 2 → 遅延ノード 5 → バス 6 → 出力 3。
    constexpr std::size_t kDelay = kMinFeedbackDelaySamples * 2;
    const auto source = std::make_shared<IndexSource>();
    const auto with_delay_node = [&](bool extra_strip) {
        GraphDescription graph = delayed_strip(source, 0, kStripKey, extra_strip);
        std::erase_if(graph.connections,
                      [](const Connection& c) { return c.from == NodeId{2} && c.to == NodeId{3}; });
        graph.nodes.push_back(keyed(NodeSpec::make_delay(NodeId{5}, kDelay), kDelayKey));
        graph.nodes.push_back(NodeSpec::make_bus(NodeId{6}, FaderId{2}, std::nullopt));
        graph.connections.emplace_back(NodeId{2}, NodeId{5});
        graph.connections.emplace_back(NodeId{5}, NodeId{6});
        graph.connections.emplace_back(NodeId{6}, NodeId{3});
        return graph;
    };
    exchange_.publish(build(with_delay_node(false)));
    std::vector<float> all = render(4);
    exchange_.publish(build(with_delay_node(true)));
    const std::vector<float> after = render(3);
    all.insert(all.end(), after.begin(), after.end());
    for (std::size_t n = kDelay + 1; n < all.size(); ++n) {
        ASSERT_FLOAT_EQ(all[n], static_cast<float>(n - kDelay + 1)) << n;
    }
}

TEST_F(HistoryCarryOverTest, CarriesLatencyCompensationOfUnchangedPath) {
    // ソース 1 → プラグイン 2（遅延 32）→ ストリップ 3 ─┐
    // ソース 4 ─────────────────────→ ストリップ 5 ─┴→ バス 6 → 出力 7
    // ストリップ 5 → バス 6 の結線に遅延補正が入る。差し替えで補正の中身が消えると、
    // 補正の長さのあいだストリップ 5 の音が抜ける。
    constexpr std::size_t kLatency = 32;
    const auto plugin = std::make_shared<FakePluginProcessor>(PluginBusLayout{1, 1}, kLatency);
    const auto first = std::make_shared<FakeAudioSource>(FakeAudioSource::constant(0.25F));
    const auto second = std::make_shared<FakeAudioSource>(FakeAudioSource::constant(0.25F));
    const auto aligned = [&](bool extra_strip) {
        GraphDescription graph;
        graph.nodes = {
            NodeSpec::make_source(NodeId{1}, first),
            NodeSpec::make_plugin(NodeId{2}, plugin),
            keyed(NodeSpec::make_strip(NodeId{3}, FaderId{0}, std::nullopt), kStripKey),
            NodeSpec::make_source(NodeId{4}, second),
            keyed(NodeSpec::make_strip(NodeId{5}, FaderId{1}, std::nullopt), kOtherStripKey),
            keyed(NodeSpec::make_bus(NodeId{6}, FaderId{2}, std::nullopt), kBusKey),
            NodeSpec::make_output(NodeId{7}, std::nullopt)};
        graph.connections = {{NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}},
                             {NodeId{3}, NodeId{6}}, {NodeId{4}, NodeId{5}},
                             {NodeId{5}, NodeId{6}}, {NodeId{6}, NodeId{7}}};
        if (extra_strip) {
            graph.nodes.push_back(NodeSpec::make_strip(NodeId{8}, FaderId{3}, std::nullopt));
            graph.connections.emplace_back(NodeId{8}, NodeId{6});
        }
        return graph;
    };
    exchange_.publish(build(aligned(false)));
    (void)render(2);
    exchange_.publish(build(aligned(true)));
    const std::vector<float> after = render(2);
    for (std::size_t n = 0; n < after.size(); ++n) {
        ASSERT_FLOAT_EQ(after[n], 0.5F) << n;
    }
}

}  // namespace
}  // namespace vocroz::audio_core
