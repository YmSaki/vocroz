// SPDX-License-Identifier: AGPL-3.0-only
//
// プラグイン遅延補正（PDC）の計算の単体テスト。analyze_latency()（公開 API）経由で、
// 合流する経路にだけ補正を足すこと、意図した遅延（手動ディレイ、遅延ノード）を数えないこと、
// 上限を超える遅延を拒否することを検証する。

#include "latency_planner.h"

#include <memory>
#include <utility>

#include <gtest/gtest.h>

#include "vocroz/audio_core/constants.h"
#include "vocroz/audio_core/latency_compensation.h"

#include "fakes/fake_audio_source.h"
#include "fakes/fake_plugin_processor.h"

namespace vocroz::audio_core {
namespace {

constexpr PluginBusLayout kMono{1, 1};
constexpr PluginBusLayout kStereo{2, 2};

std::shared_ptr<AudioSource> silent_source() {
    return std::make_shared<FakeAudioSource>(FakeAudioSource::constant(0.0F));
}

NodeSpec plugin(NodeId id, PluginBusLayout layout, std::size_t latency) {
    return NodeSpec::make_plugin(id, std::make_shared<FakePluginProcessor>(layout, latency));
}

LatencyReport analyze(const GraphDescription& graph) {
    auto report = analyze_latency(graph);
    EXPECT_TRUE(report.has_value());
    return report.has_value() ? std::move(report).value() : LatencyReport{};
}

// ソース 1 → プラグイン 2（遅延 latency）→ ストリップ 3 ─┐
// ソース 4 ─────────────────────→ ストリップ 5 ─┴→ バス 6 → 出力 7
GraphDescription two_strips_into_bus(std::size_t latency) {
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_source(NodeId{1}, silent_source()),
                   plugin(NodeId{2}, kMono, latency),
                   NodeSpec::make_strip(NodeId{3}, FaderId{0}, std::nullopt),
                   NodeSpec::make_source(NodeId{4}, silent_source()),
                   NodeSpec::make_strip(NodeId{5}, FaderId{1}, std::nullopt),
                   NodeSpec::make_bus(NodeId{6}, FaderId{2}, std::nullopt),
                   NodeSpec::make_output(NodeId{7}, std::nullopt)};
    graph.connections = {{NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}}, {NodeId{3}, NodeId{6}},
                         {NodeId{4}, NodeId{5}}, {NodeId{5}, NodeId{6}}, {NodeId{6}, NodeId{7}}};
    return graph;
}

TEST(LatencyPlannerTest, CompensatesOnlyTheShorterPathWhereTheyMerge) {
    const LatencyReport report = analyze(two_strips_into_bus(64));
    // 結線の並び: 0:1→2, 1:2→3, 2:3→6, 3:4→5, 4:5→6, 5:6→7
    EXPECT_EQ(report.compensation_samples[2], 0U);  // 遅延のある経路はそのまま
    EXPECT_EQ(report.compensation_samples[4], 64U);  // 遅延のない経路をバスの手前でそろえる
    for (const std::size_t c : {0U, 1U, 3U, 5U}) {
        EXPECT_EQ(report.compensation_samples[c], 0U) << c;
    }
    EXPECT_EQ(report.node_latency_samples[1], 64U);  // プラグインの出力
    EXPECT_EQ(report.node_latency_samples[6], 64U);  // 出力で聞こえる遅延
}

TEST(LatencyPlannerTest, DoesNotDelayPathsThatNeverMerge) {
    // ソース 1 → プラグイン 2 → バス 3 → 出力 4（チャンネル 0-1）
    // ソース 5 → バス 6 → 出力 7（チャンネル 2-3）。別の出力へ向かうので補正しない。
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_source(NodeId{1}, silent_source()),
                   plugin(NodeId{2}, PluginBusLayout{1, 2}, 128),
                   NodeSpec::make_bus(NodeId{3}, FaderId{0}, std::nullopt),
                   NodeSpec::make_output(NodeId{4}, std::nullopt, 0),
                   NodeSpec::make_source(NodeId{5}, silent_source()),
                   NodeSpec::make_bus(NodeId{6}, FaderId{1}, std::nullopt),
                   NodeSpec::make_output(NodeId{7}, std::nullopt, 2)};
    graph.connections = {{NodeId{1}, NodeId{2}},
                         {NodeId{2}, NodeId{3}},
                         {NodeId{3}, NodeId{4}},
                         {NodeId{5}, NodeId{6}},
                         {NodeId{6}, NodeId{7}}};
    const LatencyReport report = analyze(graph);
    for (const std::size_t compensation : report.compensation_samples) {
        EXPECT_EQ(compensation, 0U);
    }
    EXPECT_EQ(report.node_latency_samples[3], 128U);
    EXPECT_EQ(report.node_latency_samples[6], 0U);
}

TEST(LatencyPlannerTest, AddsLatencyOfPluginsInSeries) {
    // ソース 1 → プラグイン 2（10）→ プラグイン 3（20）→ バス 4 ← ソース 5
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_source(NodeId{1}, silent_source()), plugin(NodeId{2}, kMono, 10),
                   plugin(NodeId{3}, PluginBusLayout{1, 2}, 20),
                   NodeSpec::make_bus(NodeId{4}, FaderId{0}, std::nullopt),
                   NodeSpec::make_source(NodeId{5}, silent_source())};
    graph.connections = {{NodeId{1}, NodeId{2}},
                         {NodeId{2}, NodeId{3}},
                         {NodeId{3}, NodeId{4}},
                         {NodeId{5}, NodeId{4}}};
    const LatencyReport report = analyze(graph);
    EXPECT_EQ(report.node_latency_samples[2], 30U);
    EXPECT_EQ(report.compensation_samples[3], 30U);
}

TEST(LatencyPlannerTest, CompensatesPreFaderSendsLikeOtherConnections) {
    // ストリップ 3 のプリフェーダーのセンドがキューバス 6 で、遅延のないストリップ 5 と合流する。
    GraphDescription graph = two_strips_into_bus(48);
    graph.nodes.push_back(NodeSpec::make_bus(NodeId{8}, FaderId{3}, std::nullopt));
    graph.connections.push_back({NodeId{3}, NodeId{8}, SendTap::kPreFader, FaderId{4}});
    graph.connections.push_back({NodeId{5}, NodeId{8}, SendTap::kPreFader, FaderId{5}});
    const LatencyReport report = analyze(graph);
    EXPECT_EQ(report.compensation_samples[6], 0U);
    EXPECT_EQ(report.compensation_samples[7], 48U);
}

TEST(LatencyPlannerTest, DoesNotCountIntentionalDelays) {
    // ストリップの手動ディレイは同期のための意図した遅延なので、合流しても打ち消さない。
    GraphDescription graph = two_strips_into_bus(0);
    graph.nodes[2] = NodeSpec::make_strip(NodeId{3}, FaderId{0}, std::nullopt, 480);
    const LatencyReport report = analyze(graph);
    for (const std::size_t compensation : report.compensation_samples) {
        EXPECT_EQ(compensation, 0U);
    }
}

TEST(LatencyPlannerTest, LeavesFeedbackPathsThroughDelayNodesUncompensated) {
    // バス 1 → プラグイン 2（遅延 32）→ バス 3 → 遅延ノード 4 → バス 1（承認済みのループ）
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_bus(NodeId{1}, FaderId{0}, std::nullopt),
                   plugin(NodeId{2}, kStereo, 32),
                   NodeSpec::make_bus(NodeId{3}, FaderId{1}, std::nullopt),
                   NodeSpec::make_delay(NodeId{4}, kBlockSize)};
    graph.connections = {{NodeId{1}, NodeId{2}},
                         {NodeId{2}, NodeId{3}},
                         {NodeId{3}, NodeId{4}},
                         {NodeId{4}, NodeId{1}}};
    graph.feedback_approved = true;
    const LatencyReport report = analyze(graph);
    for (const std::size_t compensation : report.compensation_samples) {
        EXPECT_EQ(compensation, 0U);
    }
    EXPECT_EQ(report.node_latency_samples[3], 0U);  // 遅延ノードの出力は 0 から数え直す
}

TEST(LatencyPlannerTest, RejectsLatencyBeyondTheLimit) {
    auto too_long = analyze_latency(two_strips_into_bus(kMaxLatencyCompensationSamples + 1));
    ASSERT_FALSE(too_long.has_value());
    EXPECT_EQ(too_long.error(), GraphError::kLatencyOutOfRange);

    // 1 つずつは上限以内でも、直列につないだ合計が上限を超えれば拒否する。
    GraphDescription series;
    series.nodes = {NodeSpec::make_source(NodeId{1}, silent_source()),
                    plugin(NodeId{2}, kMono, kMaxLatencyCompensationSamples),
                    plugin(NodeId{3}, kMono, 1)};
    series.connections = {{NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}}};
    auto summed = analyze_latency(series);
    ASSERT_FALSE(summed.has_value());
    EXPECT_EQ(summed.error(), GraphError::kLatencyOutOfRange);

    EXPECT_TRUE(analyze_latency(two_strips_into_bus(kMaxLatencyCompensationSamples)).has_value());
}

}  // namespace
}  // namespace vocroz::audio_core
