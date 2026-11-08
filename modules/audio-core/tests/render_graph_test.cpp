// SPDX-License-Identifier: AGPL-3.0-only
//
// RenderGraph の単体テスト。記述の検証（ループ、チャンネル数、識別子）と、処理順と加算を検証する。

#include "render_graph.h"

#include <memory>

#include <gtest/gtest.h>

#include "fakes/fake_audio_source.h"

namespace vocroz::audio_core {
namespace {

constexpr std::size_t kCapacity = 8;

std::shared_ptr<AudioSource> constant_source(float value) {
    return std::make_shared<FakeAudioSource>(FakeAudioSource::constant(value));
}

GraphError build_error(const GraphDescription& description) {
    auto result = RenderGraph::build(description, kCapacity, kCapacity);
    EXPECT_FALSE(result.has_value());
    return result.has_value() ? GraphError::kCycle : result.error();
}

// ソース → ストリップ → バス → 出力 の最小構成。
GraphDescription simple_chain(float value) {
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_output(NodeId{4}, MeterId{2}),
                   NodeSpec::make_bus(NodeId{3}, FaderId{1}, MeterId{1}),
                   NodeSpec::make_strip(NodeId{2}, FaderId{0}, MeterId{0}),
                   NodeSpec::make_source(NodeId{1}, constant_source(value))};
    graph.connections = {{NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}}, {NodeId{3}, NodeId{4}}};
    return graph;
}

void unmute(FaderBank& faders, FaderId fader) {
    ParameterChange change;
    change.kind = ParameterChange::Kind::kMute;
    change.fader = fader;
    change.muted = false;
    faders.apply(change);
}

TEST(RenderGraphTest, BuildsEmptyGraphWithoutOutput) {
    auto graph = RenderGraph::build({}, 0, 0);
    ASSERT_TRUE(graph.has_value());
    FaderBank faders(0, 1);
    MeterBank meters(0);
    EXPECT_EQ(graph.value()->process(kBlockSize, faders, meters), nullptr);
}

TEST(RenderGraphTest, ProcessesInTopologicalOrderRegardlessOfDeclarationOrder) {
    auto graph = RenderGraph::build(simple_chain(0.25F), kCapacity, kCapacity);
    ASSERT_TRUE(graph.has_value());
    FaderBank faders(kCapacity, 1);
    MeterBank meters(kCapacity);
    unmute(faders, FaderId{0});
    unmute(faders, FaderId{1});

    (void)graph.value()->process(kBlockSize, faders, meters);  // ランプ（1 サンプル）を終わらせる
    const float* const* out = graph.value()->process(kBlockSize, faders, meters);
    ASSERT_NE(out, nullptr);
    // モノラルのストリップは、ステレオのバスの両チャンネルへ同じ値で加わる。
    EXPECT_FLOAT_EQ(out[0][kBlockSize - 1], 0.25F);
    EXPECT_FLOAT_EQ(out[1][kBlockSize - 1], 0.25F);
}

TEST(RenderGraphTest, SumsMultipleInputsIntoBus) {
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_source(NodeId{1}, constant_source(0.1F)),
                   NodeSpec::make_source(NodeId{2}, constant_source(0.2F)),
                   NodeSpec::make_strip(NodeId{3}, FaderId{0}, std::nullopt),
                   NodeSpec::make_strip(NodeId{4}, FaderId{1}, std::nullopt),
                   NodeSpec::make_bus(NodeId{5}, FaderId{2}, std::nullopt),
                   NodeSpec::make_output(NodeId{6}, std::nullopt)};
    graph.connections = {{NodeId{1}, NodeId{3}},
                         {NodeId{2}, NodeId{4}},
                         {NodeId{3}, NodeId{5}},
                         {NodeId{4}, NodeId{5}},
                         {NodeId{5}, NodeId{6}}};
    auto built = RenderGraph::build(graph, kCapacity, kCapacity);
    ASSERT_TRUE(built.has_value());
    FaderBank faders(kCapacity, 1);
    MeterBank meters(kCapacity);
    for (std::uint16_t id = 0; id < 3; ++id) {
        unmute(faders, FaderId{id});
    }
    (void)built.value()->process(kBlockSize, faders, meters);
    const float* const* out = built.value()->process(kBlockSize, faders, meters);
    EXPECT_NEAR(out[0][0], 0.3F, 1e-6F);
}

TEST(RenderGraphTest, MeasuresStripMeterBeforeFader) {
    auto graph = RenderGraph::build(simple_chain(0.5F), kCapacity, kCapacity);
    ASSERT_TRUE(graph.has_value());
    FaderBank faders(kCapacity, 1);  // すべてミュートのまま
    MeterBank meters(kCapacity);
    (void)graph.value()->process(kBlockSize, faders, meters);
    // チャンネルがミュートでも、プリフェーダーのメーターは入力を測る。
    EXPECT_FLOAT_EQ(meters.read(MeterId{0}).peak, 0.5F);
    EXPECT_EQ(meters.read(MeterId{1}).peak, 0.0F);  // バスはポストフェーダー
    EXPECT_EQ(meters.read(MeterId{2}).peak, 0.0F);
}

TEST(RenderGraphTest, RejectsFeedbackLoop) {
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_bus(NodeId{1}, FaderId{0}, std::nullopt),
                   NodeSpec::make_bus(NodeId{2}, FaderId{1}, std::nullopt),
                   NodeSpec::make_bus(NodeId{3}, FaderId{2}, std::nullopt)};
    graph.connections = {{NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}}, {NodeId{3}, NodeId{1}}};
    EXPECT_EQ(build_error(graph), GraphError::kCycle);
}

TEST(RenderGraphTest, RejectsSelfLoop) {
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_bus(NodeId{1}, FaderId{0}, std::nullopt)};
    graph.connections = {{NodeId{1}, NodeId{1}}};
    EXPECT_EQ(build_error(graph), GraphError::kCycle);
}

TEST(RenderGraphTest, RejectsStereoIntoMonoStrip) {
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_bus(NodeId{1}, FaderId{0}, std::nullopt),
                   NodeSpec::make_strip(NodeId{2}, FaderId{1}, std::nullopt)};
    graph.connections = {{NodeId{1}, NodeId{2}}};
    EXPECT_EQ(build_error(graph), GraphError::kChannelMismatch);
}

TEST(RenderGraphTest, RejectsInvalidDescriptions) {
    GraphDescription duplicate_node = simple_chain(0.0F);
    duplicate_node.nodes.push_back(NodeSpec::make_limiter(NodeId{1}));
    EXPECT_EQ(build_error(duplicate_node), GraphError::kDuplicateNodeId);

    GraphDescription unknown = simple_chain(0.0F);
    unknown.connections.push_back({NodeId{1}, NodeId{99}});
    EXPECT_EQ(build_error(unknown), GraphError::kUnknownNode);

    GraphDescription into_source = simple_chain(0.0F);
    into_source.connections.push_back({NodeId{2}, NodeId{1}});
    EXPECT_EQ(build_error(into_source), GraphError::kInvalidConnection);

    GraphDescription doubled = simple_chain(0.0F);
    doubled.connections.push_back({NodeId{1}, NodeId{2}});
    EXPECT_EQ(build_error(doubled), GraphError::kInvalidConnection);

    GraphDescription no_source;
    no_source.nodes = {NodeSpec::make_source(NodeId{1}, nullptr)};
    EXPECT_EQ(build_error(no_source), GraphError::kMissingSource);

    GraphDescription fader_range;
    fader_range.nodes = {NodeSpec::make_strip(NodeId{1}, FaderId{kCapacity}, std::nullopt)};
    EXPECT_EQ(build_error(fader_range), GraphError::kFaderOutOfRange);

    GraphDescription meter_range;
    meter_range.nodes = {NodeSpec::make_output(NodeId{1}, MeterId{kCapacity})};
    EXPECT_EQ(build_error(meter_range), GraphError::kMeterOutOfRange);

    GraphDescription shared_fader;
    shared_fader.nodes = {NodeSpec::make_strip(NodeId{1}, FaderId{0}, std::nullopt),
                          NodeSpec::make_strip(NodeId{2}, FaderId{0}, std::nullopt)};
    EXPECT_EQ(build_error(shared_fader), GraphError::kDuplicateFader);

    GraphDescription shared_meter;
    shared_meter.nodes = {NodeSpec::make_strip(NodeId{1}, FaderId{0}, MeterId{0}),
                          NodeSpec::make_strip(NodeId{2}, FaderId{1}, MeterId{0})};
    EXPECT_EQ(build_error(shared_meter), GraphError::kDuplicateMeter);

    GraphDescription missing_fader;
    NodeSpec strip = NodeSpec::make_strip(NodeId{1}, FaderId{0}, std::nullopt);
    strip.fader.reset();
    missing_fader.nodes = {strip};
    EXPECT_EQ(build_error(missing_fader), GraphError::kMissingFader);

    GraphDescription two_outputs;
    two_outputs.nodes = {NodeSpec::make_output(NodeId{1}, std::nullopt),
                         NodeSpec::make_output(NodeId{2}, std::nullopt)};
    EXPECT_EQ(build_error(two_outputs), GraphError::kTooManyOutputs);
}

}  // namespace
}  // namespace vocroz::audio_core
