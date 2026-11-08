// SPDX-License-Identifier: AGPL-3.0-only
//
// analyze_feedback() の単体テスト。ループの有無、ループに含まれるノード、
// 遅延ノードを通るループとそうでないループの区別を検証する。

#include "vocroz/audio_core/feedback_analysis.h"

#include <gtest/gtest.h>

#include "vocroz/audio_core/constants.h"

namespace vocroz::audio_core {
namespace {

NodeSpec bus(std::uint32_t id) {
    return NodeSpec::make_bus(NodeId{id}, FaderId{static_cast<std::uint16_t>(id)}, std::nullopt);
}

NodeSpec delay(std::uint32_t id) {
    return NodeSpec::make_delay(NodeId{id}, kMinFeedbackDelaySamples);
}

std::vector<NodeId> ids(std::initializer_list<std::uint32_t> values) {
    std::vector<NodeId> result;
    for (const std::uint32_t value : values) {
        result.push_back(NodeId{value});
    }
    return result;
}

TEST(FeedbackAnalysisTest, ReportsNoLoopsForTreeAndDiamond) {
    GraphDescription graph;
    graph.nodes = {bus(1), bus(2), bus(3), bus(4)};
    // 1 → 2 → 4 と 1 → 3 → 4 の菱形。合流はループではない。
    graph.connections = {{NodeId{1}, NodeId{2}},
                         {NodeId{1}, NodeId{3}},
                         {NodeId{2}, NodeId{4}},
                         {NodeId{3}, NodeId{4}}};
    const FeedbackReport report = analyze_feedback(graph);
    EXPECT_FALSE(report.has_loops());
    EXPECT_TRUE(report.all_loops_delayed());
}

TEST(FeedbackAnalysisTest, ReportsOnlyNodesInsideTheLoop) {
    GraphDescription graph;
    graph.nodes = {bus(5), bus(2), bus(3), bus(4), bus(1)};
    // 1 → 2 → 3 → 2 のループと、ループの外の 3 → 4、5 → 1。
    graph.connections = {{NodeId{5}, NodeId{1}},
                         {NodeId{1}, NodeId{2}},
                         {NodeId{2}, NodeId{3}},
                         {NodeId{3}, NodeId{2}},
                         {NodeId{3}, NodeId{4}}};
    const FeedbackReport report = analyze_feedback(graph);
    EXPECT_EQ(report.looped_nodes, ids({2, 3}));
    EXPECT_EQ(report.undelayed_nodes, ids({2, 3}));
}

TEST(FeedbackAnalysisTest, ReportsSelfLoop) {
    GraphDescription graph;
    graph.nodes = {bus(1), bus(2)};
    graph.connections = {{NodeId{1}, NodeId{1}}, {NodeId{1}, NodeId{2}}};
    const FeedbackReport report = analyze_feedback(graph);
    EXPECT_EQ(report.looped_nodes, ids({1}));
    EXPECT_FALSE(report.all_loops_delayed());
}

TEST(FeedbackAnalysisTest, LoopThroughDelayNodeIsDelayed) {
    GraphDescription graph;
    graph.nodes = {bus(1), delay(2), bus(3)};
    graph.connections = {{NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}}, {NodeId{3}, NodeId{1}}};
    const FeedbackReport report = analyze_feedback(graph);
    EXPECT_EQ(report.looped_nodes, ids({1, 2, 3}));
    EXPECT_TRUE(report.all_loops_delayed());
}

TEST(FeedbackAnalysisTest, DetectsUndelayedLoopBesideDelayedLoop) {
    GraphDescription graph;
    graph.nodes = {bus(1), delay(2), bus(3)};
    // 1 → 2(遅延) → 1 は遅延のあるループ。1 → 3 → 1 は遅延のないループ。
    graph.connections = {{NodeId{1}, NodeId{2}},
                         {NodeId{2}, NodeId{1}},
                         {NodeId{1}, NodeId{3}},
                         {NodeId{3}, NodeId{1}}};
    const FeedbackReport report = analyze_feedback(graph);
    EXPECT_EQ(report.looped_nodes, ids({1, 2, 3}));
    EXPECT_EQ(report.undelayed_nodes, ids({1, 3}));
}

TEST(FeedbackAnalysisTest, IgnoresConnectionsToUnknownNodes) {
    GraphDescription graph;
    graph.nodes = {bus(1)};
    graph.connections = {{NodeId{1}, NodeId{9}}, {NodeId{9}, NodeId{1}}};
    EXPECT_FALSE(analyze_feedback(graph).has_loops());
}

TEST(FeedbackAnalysisTest, HandlesLongChainWithoutRecursion) {
    // 再帰で辿るとスタックが溢れる長さのループでも検出できる。
    constexpr std::uint32_t kLength = 20'000;
    GraphDescription graph;
    for (std::uint32_t i = 0; i < kLength; ++i) {
        graph.nodes.push_back(NodeSpec::make_limiter(NodeId{i}));
        graph.connections.push_back({NodeId{i}, NodeId{(i + 1) % kLength}});
    }
    const FeedbackReport report = analyze_feedback(graph);
    EXPECT_EQ(report.looped_nodes.size(), kLength);
}

}  // namespace
}  // namespace vocroz::audio_core
