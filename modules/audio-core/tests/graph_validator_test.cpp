// SPDX-License-Identifier: AGPL-3.0-only
//
// plan_graph() の単体テスト。Beta1 のルーティングで加えた規則（センド、遅延、複数の出力、
// フィードバック経路の承認）と、処理順の決め方を検証する。
// Alpha からある規則（ノードと識別子の検証）は render_graph_test.cpp が RenderGraph::build()
// 経由で検証する。

#include "graph_validator.h"

#include <algorithm>
#include <memory>

#include <gtest/gtest.h>

#include "vocroz/audio_core/constants.h"

#include "fakes/fake_audio_source.h"

namespace vocroz::audio_core {
namespace {

constexpr std::size_t kCapacity = 16;

GraphError plan_error(const GraphDescription& description) {
    auto result = plan_graph(description, kCapacity, kCapacity);
    EXPECT_FALSE(result.has_value());
    return result.has_value() ? GraphError::kCycle : result.error();
}

// ソース → ストリップ → バス の構成に、検証したい結線やノードを足して使う。
GraphDescription strip_and_bus() {
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_source(NodeId{1}, std::make_shared<FakeAudioSource>(
                                                        FakeAudioSource::constant(0.0F))),
                   NodeSpec::make_strip(NodeId{2}, FaderId{0}, std::nullopt),
                   NodeSpec::make_bus(NodeId{3}, FaderId{1}, std::nullopt)};
    graph.connections = {{NodeId{1}, NodeId{2}}};
    return graph;
}

// バス 1 → 遅延 2 → バス 3 → バス 1 のループ。
GraphDescription delayed_loop() {
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_bus(NodeId{1}, FaderId{0}, std::nullopt),
                   NodeSpec::make_delay(NodeId{2}, kMinFeedbackDelaySamples),
                   NodeSpec::make_bus(NodeId{3}, FaderId{1}, std::nullopt)};
    graph.connections = {{NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}}, {NodeId{3}, NodeId{1}}};
    return graph;
}

TEST(GraphValidatorTest, AcceptsPreAndPostFaderSendsWithLevels) {
    GraphDescription graph = strip_and_bus();
    graph.nodes.push_back(NodeSpec::make_bus(NodeId{4}, FaderId{2}, std::nullopt));
    graph.connections.push_back({NodeId{2}, NodeId{3}, SendTap::kPostFader, FaderId{3}});
    graph.connections.push_back({NodeId{2}, NodeId{4}, SendTap::kPreFader, FaderId{4}});
    auto plan = plan_graph(graph, kCapacity, kCapacity);
    ASSERT_TRUE(plan.has_value());
    EXPECT_TRUE(plan.value().has_pre_fader_send[1]);   // ストリップ
    EXPECT_FALSE(plan.value().has_pre_fader_send[2]);  // バス
}

TEST(GraphValidatorTest, RejectsPreFaderSendWithoutLevel) {
    // チャンネルのフェーダーの前から送るので、センドレベルがないと新規参加時の
    // 初期ミュートを守れない。
    GraphDescription graph = strip_and_bus();
    graph.connections.push_back({NodeId{2}, NodeId{3}, SendTap::kPreFader, std::nullopt});
    EXPECT_EQ(plan_error(graph), GraphError::kMissingSendLevel);
}

TEST(GraphValidatorTest, RejectsPreFaderSendFromNodeWithoutFader) {
    GraphDescription graph = strip_and_bus();
    graph.nodes.push_back(NodeSpec::make_limiter(NodeId{4}));
    graph.connections.push_back({NodeId{3}, NodeId{4}});
    graph.nodes.push_back(NodeSpec::make_output(NodeId{5}, std::nullopt));
    graph.connections.push_back({NodeId{4}, NodeId{5}, SendTap::kPreFader, FaderId{5}});
    EXPECT_EQ(plan_error(graph), GraphError::kInvalidSendTap);
}

TEST(GraphValidatorTest, RejectsSendLevelSharedWithNodeFader) {
    GraphDescription graph = strip_and_bus();
    graph.connections.push_back({NodeId{2}, NodeId{3}, SendTap::kPostFader, FaderId{1}});
    EXPECT_EQ(plan_error(graph), GraphError::kDuplicateFader);

    GraphDescription out_of_range = strip_and_bus();
    out_of_range.connections.push_back(
        {NodeId{2}, NodeId{3}, SendTap::kPostFader, FaderId{kCapacity}});
    EXPECT_EQ(plan_error(out_of_range), GraphError::kFaderOutOfRange);
}

TEST(GraphValidatorTest, ChecksDelayRanges) {
    GraphDescription strip_max;
    strip_max.nodes = {
        NodeSpec::make_strip(NodeId{1}, FaderId{0}, std::nullopt, kMaxInputDelaySamples)};
    EXPECT_TRUE(plan_graph(strip_max, kCapacity, kCapacity).has_value());

    GraphDescription strip_over;
    strip_over.nodes = {
        NodeSpec::make_strip(NodeId{1}, FaderId{0}, std::nullopt, kMaxInputDelaySamples + 1)};
    EXPECT_EQ(plan_error(strip_over), GraphError::kDelayOutOfRange);

    // 遅延ノードは前のブロックの入力から出力を作るので、1 ブロックより短くできない。
    GraphDescription delay_short;
    delay_short.nodes = {NodeSpec::make_delay(NodeId{1}, kMinFeedbackDelaySamples - 1)};
    EXPECT_EQ(plan_error(delay_short), GraphError::kDelayOutOfRange);

    GraphDescription delay_long;
    delay_long.nodes = {NodeSpec::make_delay(NodeId{1}, kMaxFeedbackDelaySamples + 1)};
    EXPECT_EQ(plan_error(delay_long), GraphError::kDelayOutOfRange);
}

TEST(GraphValidatorTest, AcceptsOutputsOnSeparateChannelPairs) {
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_output(NodeId{1}, std::nullopt, 0),
                   NodeSpec::make_output(NodeId{2}, std::nullopt, 2),
                   NodeSpec::make_output(NodeId{3}, std::nullopt, kMaxOutputChannels - 2)};
    EXPECT_TRUE(plan_graph(graph, kCapacity, kCapacity).has_value());
}

TEST(GraphValidatorTest, RejectsOverlappingOrOutOfRangeOutputs) {
    // チャンネル 0-1 と 1-2 は R と L が重なる。
    GraphDescription overlapping;
    overlapping.nodes = {NodeSpec::make_output(NodeId{1}, std::nullopt, 0),
                         NodeSpec::make_output(NodeId{2}, std::nullopt, 1)};
    EXPECT_EQ(plan_error(overlapping), GraphError::kOverlappingOutputs);

    GraphDescription out_of_range;
    out_of_range.nodes = {NodeSpec::make_output(NodeId{1}, std::nullopt, kMaxOutputChannels - 1)};
    EXPECT_EQ(plan_error(out_of_range), GraphError::kOutputChannelOutOfRange);
}

TEST(GraphValidatorTest, RejectsDelayedLoopUntilApproved) {
    GraphDescription graph = delayed_loop();
    EXPECT_EQ(plan_error(graph), GraphError::kUnapprovedFeedback);
    graph.feedback_approved = true;
    EXPECT_TRUE(plan_graph(graph, kCapacity, kCapacity).has_value());
}

TEST(GraphValidatorTest, RejectsUndelayedLoopEvenWhenApproved) {
    GraphDescription graph = delayed_loop();
    graph.feedback_approved = true;
    // 遅延ノードを迂回する 1 → 3 を足すと、遅延のないループ 1 → 3 → 1 ができる。
    graph.connections.push_back({NodeId{1}, NodeId{3}});
    EXPECT_EQ(plan_error(graph), GraphError::kCycle);
}

TEST(GraphValidatorTest, OrdersDelayNodeBeforeItsLoopInputs) {
    GraphDescription graph = delayed_loop();
    graph.feedback_approved = true;
    auto plan = plan_graph(graph, kCapacity, kCapacity);
    ASSERT_TRUE(plan.has_value());
    const std::vector<std::size_t>& order = plan.value().order;
    const auto position = [&](std::size_t spec_index) {
        return std::find(order.begin(), order.end(), spec_index) - order.begin();
    };
    // 遅延 2 の出力が先に決まり、バス 3、バス 1 の順に処理する。
    EXPECT_LT(position(1), position(2));
    EXPECT_LT(position(2), position(0));
}

TEST(GraphValidatorTest, DelayNodeOutsideLoopNeedsNoApproval) {
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_bus(NodeId{1}, FaderId{0}, std::nullopt),
                   NodeSpec::make_delay(NodeId{2}, kMinFeedbackDelaySamples)};
    graph.connections = {{NodeId{1}, NodeId{2}}};
    EXPECT_TRUE(plan_graph(graph, kCapacity, kCapacity).has_value());
}

}  // namespace
}  // namespace vocroz::audio_core
