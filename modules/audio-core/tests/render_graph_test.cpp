// SPDX-License-Identifier: AGPL-3.0-only
//
// RenderGraph の単体テスト。記述の検証（ループ、チャンネル数、識別子）と、処理順と加算を検証する。
// Beta1 のルーティング（センド、手動ディレイ、遅延ノードを通るループ、複数のバスと出力）の
// 信号の流れもここで検証する。

#include "render_graph.h"

#include <initializer_list>
#include <memory>
#include <span>
#include <utility>

#include <gtest/gtest.h>

#include "vocroz/audio_core/constants.h"

#include "fakes/fake_audio_source.h"
#include "fakes/fake_plugin_processor.h"

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

void unmute_all(FaderBank& faders, std::initializer_list<std::uint16_t> ids) {
    for (const std::uint16_t id : ids) {
        unmute(faders, FaderId{id});
    }
}

void set_gain(FaderBank& faders, FaderId fader, float linear) {
    ParameterChange change;
    change.kind = ParameterChange::Kind::kGain;
    change.fader = fader;
    change.gain = linear;
    faders.apply(change);
}

// 処理結果から、指定のチャンネルの組に書く出力ノードを探す。
const OutputBlock* find_output(std::span<const OutputBlock> outputs, std::size_t device_channel) {
    for (const OutputBlock& output : outputs) {
        if (output.device_channel == device_channel) {
            return &output;
        }
    }
    return nullptr;
}

// ソース 1 → ストリップ 2 → ポストフェーダー → Main バス 3 → 出力 4（チャンネル 0-1）と、
// ストリップ 2 → プリフェーダー（センドレベル 4）→ キューバス 5 → 出力 6（チャンネル 2-3）。
GraphDescription main_and_cue(std::shared_ptr<AudioSource> source) {
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_source(NodeId{1}, std::move(source)),
                   NodeSpec::make_strip(NodeId{2}, FaderId{0}, std::nullopt),
                   NodeSpec::make_bus(NodeId{3}, FaderId{1}, std::nullopt),
                   NodeSpec::make_output(NodeId{4}, std::nullopt, 0),
                   NodeSpec::make_bus(NodeId{5}, FaderId{2}, std::nullopt),
                   NodeSpec::make_output(NodeId{6}, std::nullopt, 2)};
    graph.connections = {{NodeId{1}, NodeId{2}},
                         {NodeId{2}, NodeId{3}},
                         {NodeId{3}, NodeId{4}},
                         {NodeId{2}, NodeId{5}, SendTap::kPreFader, FaderId{4}},
                         {NodeId{5}, NodeId{6}}};
    return graph;
}

// ステレオのソース → ステレオのストリップ（手動ディレイ付き）→ バス → 出力。
GraphDescription stereo_chain(std::size_t delay_samples) {
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_source(NodeId{1}, std::make_shared<FakeAudioSource>(
                                                        FakeAudioSource::stereo(0.25F, -0.5F))),
                   NodeSpec::make_strip(NodeId{2}, FaderId{0}, MeterId{0}, delay_samples, 2),
                   NodeSpec::make_bus(NodeId{3}, FaderId{1}, std::nullopt),
                   NodeSpec::make_output(NodeId{4}, std::nullopt)};
    graph.connections = {{NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}}, {NodeId{3}, NodeId{4}}};
    return graph;
}

TEST(RenderGraphTest, StereoStripKeepsLeftAndRightSeparate) {
    auto graph = RenderGraph::build(stereo_chain(0), kCapacity, kCapacity);
    ASSERT_TRUE(graph.has_value());
    FaderBank faders(kCapacity, 1);
    MeterBank meters(kCapacity);
    unmute_all(faders, {0, 1});
    (void)graph.value()->process(kBlockSize, faders, meters);
    const auto out = graph.value()->process(kBlockSize, faders, meters);
    ASSERT_EQ(out.size(), 1U);
    EXPECT_FLOAT_EQ(out[0].left[kBlockSize - 1], 0.25F);
    EXPECT_FLOAT_EQ(out[0].right[kBlockSize - 1], -0.5F);
}

TEST(RenderGraphTest, StereoStripDelaysBothChannels) {
    constexpr std::size_t kDelay = 10;
    auto graph = RenderGraph::build(stereo_chain(kDelay), kCapacity, kCapacity);
    ASSERT_TRUE(graph.has_value());
    FaderBank faders(kCapacity, 1);
    MeterBank meters(kCapacity);
    unmute_all(faders, {0, 1});
    const auto out = graph.value()->process(kBlockSize, faders, meters);
    ASSERT_EQ(out.size(), 1U);
    EXPECT_FLOAT_EQ(out[0].left[kDelay - 1], 0.0F);
    EXPECT_FLOAT_EQ(out[0].right[kDelay - 1], 0.0F);
    EXPECT_NE(out[0].left[kBlockSize - 1], 0.0F);
    EXPECT_NE(out[0].right[kBlockSize - 1], 0.0F);
}

TEST(RenderGraphTest, RejectsStereoSourceIntoMonoStrip) {
    GraphDescription graph = stereo_chain(0);
    graph.nodes[1] = NodeSpec::make_strip(NodeId{2}, FaderId{0}, MeterId{0});
    EXPECT_EQ(build_error(graph), GraphError::kChannelMismatch);
}

TEST(RenderGraphTest, MonoSourceFeedsBothChannelsOfStereoStrip) {
    GraphDescription graph = simple_chain(0.25F);
    graph.nodes[2] = NodeSpec::make_strip(NodeId{2}, FaderId{0}, MeterId{0}, 0, 2);
    auto built = RenderGraph::build(graph, kCapacity, kCapacity);
    ASSERT_TRUE(built.has_value());
    FaderBank faders(kCapacity, 1);
    MeterBank meters(kCapacity);
    unmute_all(faders, {0, 1});
    (void)built.value()->process(kBlockSize, faders, meters);
    const auto out = built.value()->process(kBlockSize, faders, meters);
    EXPECT_FLOAT_EQ(out[0].left[kBlockSize - 1], 0.25F);
    EXPECT_FLOAT_EQ(out[0].right[kBlockSize - 1], 0.25F);
}

TEST(RenderGraphTest, BuildsEmptyGraphWithoutOutput) {
    auto graph = RenderGraph::build({}, 0, 0);
    ASSERT_TRUE(graph.has_value());
    FaderBank faders(0, 1);
    MeterBank meters(0);
    EXPECT_TRUE(graph.value()->process(kBlockSize, faders, meters).empty());
}

TEST(RenderGraphTest, ProcessesInTopologicalOrderRegardlessOfDeclarationOrder) {
    auto graph = RenderGraph::build(simple_chain(0.25F), kCapacity, kCapacity);
    ASSERT_TRUE(graph.has_value());
    FaderBank faders(kCapacity, 1);
    MeterBank meters(kCapacity);
    unmute(faders, FaderId{0});
    unmute(faders, FaderId{1});

    (void)graph.value()->process(kBlockSize, faders, meters);  // ランプ（1 サンプル）を終わらせる
    const auto out = graph.value()->process(kBlockSize, faders, meters);
    ASSERT_EQ(out.size(), 1U);
    // モノラルのストリップは、ステレオのバスの両チャンネルへ同じ値で加わる。
    EXPECT_FLOAT_EQ(out[0].left[kBlockSize - 1], 0.25F);
    EXPECT_FLOAT_EQ(out[0].right[kBlockSize - 1], 0.25F);
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
    const auto out = built.value()->process(kBlockSize, faders, meters);
    EXPECT_NEAR(out[0].left[0], 0.3F, 1e-6F);
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

    // 複数の出力ノードは置けるが、同じ出力デバイスのチャンネルには書けない。
    GraphDescription two_outputs;
    two_outputs.nodes = {NodeSpec::make_output(NodeId{1}, std::nullopt),
                         NodeSpec::make_output(NodeId{2}, std::nullopt)};
    EXPECT_EQ(build_error(two_outputs), GraphError::kOverlappingOutputs);
}

TEST(RenderGraphTest, PreFaderSendIgnoresChannelFaderButStartsMuted) {
    auto graph = RenderGraph::build(main_and_cue(constant_source(0.5F)), kCapacity, kCapacity);
    ASSERT_TRUE(graph.has_value());
    FaderBank faders(kCapacity, 1);
    MeterBank meters(kCapacity);
    unmute(faders, FaderId{1});
    unmute(faders, FaderId{2});

    // センドレベルはミュートで始まるので、新規参加者の音はキューにも出ない（初期ミュート）。
    (void)graph.value()->process(kBlockSize, faders, meters);
    auto out = graph.value()->process(kBlockSize, faders, meters);
    EXPECT_EQ(find_output(out, 2)->left[kBlockSize - 1], 0.0F);

    // チャンネルのフェーダーをミュートのままにしても、プリフェーダーのセンドはキューへ送る。
    unmute(faders, FaderId{4});
    (void)graph.value()->process(kBlockSize, faders, meters);
    out = graph.value()->process(kBlockSize, faders, meters);
    EXPECT_EQ(find_output(out, 0)->left[kBlockSize - 1], 0.0F);
    EXPECT_FLOAT_EQ(find_output(out, 2)->left[kBlockSize - 1], 0.5F);
    EXPECT_FLOAT_EQ(find_output(out, 2)->right[kBlockSize - 1], 0.5F);
}

TEST(RenderGraphTest, PostFaderSendFollowsChannelFaderAndSendLevel) {
    GraphDescription description = main_and_cue(constant_source(0.5F));
    description.connections[3].tap = SendTap::kPostFader;
    auto graph = RenderGraph::build(description, kCapacity, kCapacity);
    ASSERT_TRUE(graph.has_value());
    FaderBank faders(kCapacity, 1);
    MeterBank meters(kCapacity);
    unmute_all(faders, {1, 2, 4});
    (void)graph.value()->process(kBlockSize, faders, meters);
    auto out = graph.value()->process(kBlockSize, faders, meters);
    EXPECT_EQ(find_output(out, 2)->left[kBlockSize - 1], 0.0F);  // チャンネルがミュート

    unmute(faders, FaderId{0});
    set_gain(faders, FaderId{0}, 0.5F);  // チャンネルのフェーダー
    set_gain(faders, FaderId{4}, 0.5F);  // センドレベル
    (void)graph.value()->process(kBlockSize, faders, meters);
    out = graph.value()->process(kBlockSize, faders, meters);
    EXPECT_FLOAT_EQ(find_output(out, 0)->left[kBlockSize - 1], 0.25F);
    EXPECT_FLOAT_EQ(find_output(out, 2)->left[kBlockSize - 1], 0.125F);
}

TEST(RenderGraphTest, MultipleBusOutputsStaySampleAligned) {
    // 同じ入力を 2 つのバスから別々の出力へ出しても、サンプルの位置がずれない。
    auto graph = RenderGraph::build(
        main_and_cue(std::make_shared<FakeAudioSource>(FakeAudioSource::sine(0.5F, 997.0F))),
        kCapacity, kCapacity);
    ASSERT_TRUE(graph.has_value());
    FaderBank faders(kCapacity, 1);
    MeterBank meters(kCapacity);
    unmute_all(faders, {0, 1, 2, 4});
    (void)graph.value()->process(kBlockSize, faders, meters);
    for (int block = 0; block < 4; ++block) {
        const auto out = graph.value()->process(kBlockSize, faders, meters);
        ASSERT_EQ(out.size(), 2U);
        for (std::size_t i = 0; i < kBlockSize; ++i) {
            ASSERT_FLOAT_EQ(find_output(out, 0)->left[i], find_output(out, 2)->left[i]);
            ASSERT_FLOAT_EQ(find_output(out, 0)->right[i], find_output(out, 2)->right[i]);
        }
    }
}

TEST(RenderGraphTest, DelaysStripByManualOffsetBeforeMeterAndFader) {
    constexpr std::size_t kOffset = 40;
    GraphDescription description = simple_chain(0.5F);
    description.nodes[2] = NodeSpec::make_strip(NodeId{2}, FaderId{0}, MeterId{0}, kOffset);
    auto graph = RenderGraph::build(description, kCapacity, kCapacity);
    ASSERT_TRUE(graph.has_value());
    FaderBank faders(kCapacity, 1);
    MeterBank meters(kCapacity);
    unmute(faders, FaderId{0});
    unmute(faders, FaderId{1});
    const auto out = graph.value()->process(kBlockSize, faders, meters);
    EXPECT_EQ(out[0].left[kOffset - 1], 0.0F);
    EXPECT_FLOAT_EQ(out[0].left[kOffset + 1], 0.5F);
}

TEST(RenderGraphTest, ApprovedFeedbackThroughDelayNodeRecirculatesOncePerDelay) {
    // ソース → ストリップ → バス A → 出力、バス A → 遅延（1 ブロック）→ バス B（0.5 倍）→ バス A。
    // バス A の出力は、ブロック n で c + 0.5 × (ブロック n-1 のバス A) になる。
    GraphDescription description;
    description.nodes = {NodeSpec::make_source(NodeId{1}, constant_source(0.25F)),
                         NodeSpec::make_strip(NodeId{2}, FaderId{0}, std::nullopt),
                         NodeSpec::make_bus(NodeId{3}, FaderId{1}, std::nullopt),
                         NodeSpec::make_delay(NodeId{4}, kMinFeedbackDelaySamples),
                         NodeSpec::make_bus(NodeId{5}, FaderId{2}, std::nullopt),
                         NodeSpec::make_output(NodeId{6}, std::nullopt)};
    description.connections = {{NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}},
                               {NodeId{3}, NodeId{4}}, {NodeId{4}, NodeId{5}},
                               {NodeId{5}, NodeId{3}}, {NodeId{3}, NodeId{6}}};
    EXPECT_EQ(build_error(description), GraphError::kUnapprovedFeedback);

    description.feedback_approved = true;
    auto graph = RenderGraph::build(description, kCapacity, kCapacity);
    ASSERT_TRUE(graph.has_value());
    FaderBank faders(kCapacity, 1);
    MeterBank meters(kCapacity);
    unmute_all(faders, {0, 1, 2});
    set_gain(faders, FaderId{2}, 0.5F);
    const float expected[] = {0.25F, 0.375F, 0.4375F};
    for (const float value : expected) {
        const auto out = graph.value()->process(kBlockSize, faders, meters);
        // 先頭のサンプルはランプ（1 サンプル）の途中を含むので、ブロックの末尾で比べる。
        EXPECT_FLOAT_EQ(out[0].left[kBlockSize - 1], value);
    }
}

// ソース 1 → プラグイン 2 → ストリップ 3 → バス 4 → 出力 5。
GraphDescription plugin_chain(std::shared_ptr<PluginProcessor> plugin, float value) {
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_source(NodeId{1}, constant_source(value)),
                   NodeSpec::make_plugin(NodeId{2}, std::move(plugin)),
                   NodeSpec::make_strip(NodeId{3}, FaderId{0}, std::nullopt),
                   NodeSpec::make_bus(NodeId{4}, FaderId{1}, std::nullopt),
                   NodeSpec::make_output(NodeId{5}, std::nullopt)};
    graph.connections = {{NodeId{1}, NodeId{2}},
                         {NodeId{2}, NodeId{3}},
                         {NodeId{3}, NodeId{4}},
                         {NodeId{4}, NodeId{5}}};
    return graph;
}

TEST(RenderGraphTest, ProcessesSignalThroughPluginNode) {
    auto plugin = std::make_shared<FakePluginProcessor>(PluginBusLayout{1, 1}, 0, 0.5F);
    auto graph = RenderGraph::build(plugin_chain(plugin, 0.4F), kCapacity, kCapacity);
    ASSERT_TRUE(graph.has_value());
    FaderBank faders(kCapacity, 1);
    MeterBank meters(kCapacity);
    unmute_all(faders, {0, 1});
    (void)graph.value()->process(kBlockSize, faders, meters);
    const auto out = graph.value()->process(kBlockSize, faders, meters);
    EXPECT_FLOAT_EQ(out[0].left[kBlockSize - 1], 0.2F);
    EXPECT_EQ(plugin->process_count(), 2U);
}

TEST(RenderGraphTest, MonoInputStereoOutputPluginFeedsStereoBus) {
    // モノラルのチャンネルにステレオのリバーブを挿す構成。バッファは入出力の多い方（2 本）。
    auto plugin = std::make_shared<FakePluginProcessor>(PluginBusLayout{1, 2});
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_source(NodeId{1}, constant_source(0.3F)),
                   NodeSpec::make_plugin(NodeId{2}, plugin),
                   NodeSpec::make_bus(NodeId{3}, FaderId{0}, std::nullopt),
                   NodeSpec::make_output(NodeId{4}, std::nullopt)};
    graph.connections = {{NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}}, {NodeId{3}, NodeId{4}}};
    auto built = RenderGraph::build(graph, kCapacity, kCapacity);
    ASSERT_TRUE(built.has_value());
    FaderBank faders(kCapacity, 1);
    MeterBank meters(kCapacity);
    unmute(faders, FaderId{0});
    (void)built.value()->process(kBlockSize, faders, meters);
    const auto out = built.value()->process(kBlockSize, faders, meters);
    EXPECT_EQ(plugin->last_channel_count(), 2U);
    EXPECT_FLOAT_EQ(out[0].left[0], 0.3F);
    EXPECT_FLOAT_EQ(out[0].right[0], 0.3F);
}

TEST(RenderGraphTest, InstrumentPluginRunsWithoutAudioInput) {
    // VSTi（入力 0）は結線の入力がなくても処理され、出力を後段へ渡す。
    auto plugin = std::make_shared<FakePluginProcessor>(PluginBusLayout{0, 2});
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_plugin(NodeId{1}, plugin),
                   NodeSpec::make_bus(NodeId{2}, FaderId{0}, std::nullopt),
                   NodeSpec::make_output(NodeId{3}, std::nullopt)};
    graph.connections = {{NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}}};
    auto built = RenderGraph::build(graph, kCapacity, kCapacity);
    ASSERT_TRUE(built.has_value());
    FaderBank faders(kCapacity, 1);
    MeterBank meters(kCapacity);
    (void)built.value()->process(kBlockSize, faders, meters);
    EXPECT_EQ(plugin->process_count(), 1U);
    EXPECT_EQ(plugin->last_channel_count(), 2U);

    // 音声入力を持たないプラグインへの結線は拒否する。
    graph.nodes.push_back(NodeSpec::make_source(NodeId{4}, constant_source(0.1F)));
    graph.connections.push_back({NodeId{4}, NodeId{1}});
    EXPECT_EQ(build_error(graph), GraphError::kInvalidConnection);
}

TEST(RenderGraphTest, MutesPluginNodeAfterFailureUntilGraphIsRebuilt) {
    auto plugin = std::make_shared<FakePluginProcessor>(PluginBusLayout{1, 1});
    plugin->fail_after(2);
    auto graph = RenderGraph::build(plugin_chain(plugin, 0.5F), kCapacity, kCapacity);
    ASSERT_TRUE(graph.has_value());
    FaderBank faders(kCapacity, 1);
    MeterBank meters(kCapacity);
    unmute_all(faders, {0, 1});
    (void)graph.value()->process(kBlockSize, faders, meters);
    EXPECT_FLOAT_EQ(graph.value()->process(kBlockSize, faders, meters)[0].left[0], 0.5F);
    // 3 回目で失敗する。素通しにせず無音にし、以降はプラグインを呼ばない。
    EXPECT_EQ(graph.value()->process(kBlockSize, faders, meters)[0].left[0], 0.0F);
    EXPECT_EQ(graph.value()->process(kBlockSize, faders, meters)[0].left[0], 0.0F);
    EXPECT_EQ(plugin->process_count(), 3U);
}

TEST(RenderGraphTest, MutesPluginNodeThatProducesNonFiniteSamples) {
    auto plugin = std::make_shared<FakePluginProcessor>(PluginBusLayout{1, 1});
    plugin->produce_nan_after(1);
    auto graph = RenderGraph::build(plugin_chain(plugin, 0.5F), kCapacity, kCapacity);
    ASSERT_TRUE(graph.has_value());
    FaderBank faders(kCapacity, 1);
    MeterBank meters(kCapacity);
    unmute_all(faders, {0, 1});
    (void)graph.value()->process(kBlockSize, faders, meters);
    const auto out = graph.value()->process(kBlockSize, faders, meters);
    for (std::size_t i = 0; i < kBlockSize; ++i) {
        ASSERT_EQ(out[0].left[i], 0.0F) << i;
    }
}

TEST(RenderGraphTest, AlignsPathsWithAndWithoutPluginLatencyAtTheBus) {
    // ソース 1 → プラグイン 2（遅延 32）→ ストリップ 3 ─┐
    // ソース 4 ─────────────────────→ ストリップ 5 ─┴→ バス 6 → 出力 7
    // PDC がなければ、最初の 32 サンプルはストリップ 5 の信号だけが先に聞こえる。
    constexpr std::size_t kLatency = 32;
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_source(NodeId{1}, constant_source(0.25F)),
                   NodeSpec::make_plugin(NodeId{2}, std::make_shared<FakePluginProcessor>(
                                                        PluginBusLayout{1, 1}, kLatency)),
                   NodeSpec::make_strip(NodeId{3}, FaderId{0}, std::nullopt),
                   NodeSpec::make_source(NodeId{4}, constant_source(0.25F)),
                   NodeSpec::make_strip(NodeId{5}, FaderId{1}, std::nullopt),
                   NodeSpec::make_bus(NodeId{6}, FaderId{2}, std::nullopt),
                   NodeSpec::make_output(NodeId{7}, std::nullopt)};
    graph.connections = {{NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}}, {NodeId{3}, NodeId{6}},
                         {NodeId{4}, NodeId{5}}, {NodeId{5}, NodeId{6}}, {NodeId{6}, NodeId{7}}};
    auto built = RenderGraph::build(graph, kCapacity, kCapacity);
    ASSERT_TRUE(built.has_value());
    FaderBank faders(kCapacity, 1);
    MeterBank meters(kCapacity);
    unmute_all(faders, {0, 1, 2});
    const auto out = built.value()->process(kBlockSize, faders, meters);
    // 先頭はフェーダーのランプ（1 サンプル）を避けて 1 から見る。
    for (std::size_t i = 1; i < kLatency; ++i) {
        ASSERT_EQ(out[0].left[i], 0.0F) << i;
    }
    for (std::size_t i = kLatency + 1; i < kBlockSize; ++i) {
        ASSERT_FLOAT_EQ(out[0].left[i], 0.5F) << i;
    }
}

TEST(RenderGraphTest, RejectsInvalidPluginNodes) {
    GraphDescription missing;
    missing.nodes = {NodeSpec::make_plugin(NodeId{1}, nullptr)};
    EXPECT_EQ(build_error(missing), GraphError::kMissingPlugin);

    for (const PluginBusLayout layout :
         {PluginBusLayout{3, 2}, PluginBusLayout{2, 0}, PluginBusLayout{2, 3}}) {
        GraphDescription invalid;
        invalid.nodes = {
            NodeSpec::make_plugin(NodeId{1}, std::make_shared<FakePluginProcessor>(layout))};
        EXPECT_EQ(build_error(invalid), GraphError::kInvalidPluginLayout);
    }

    // ステレオ出力のプラグインはモノラルのストリップへつなげない。
    GraphDescription stereo_into_strip;
    stereo_into_strip.nodes = {
        NodeSpec::make_source(NodeId{1}, constant_source(0.0F)),
        NodeSpec::make_plugin(NodeId{2},
                              std::make_shared<FakePluginProcessor>(PluginBusLayout{1, 2})),
        NodeSpec::make_strip(NodeId{3}, FaderId{0}, std::nullopt)};
    stereo_into_strip.connections = {{NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}}};
    EXPECT_EQ(build_error(stereo_into_strip), GraphError::kChannelMismatch);

    // プラグインはフェーダーを持たないので、プリフェーダーのセンドの送り元にできない。
    GraphDescription pre_fader;
    pre_fader.nodes = {NodeSpec::make_plugin(
                           NodeId{1}, std::make_shared<FakePluginProcessor>(PluginBusLayout{0, 2})),
                       NodeSpec::make_bus(NodeId{2}, FaderId{0}, std::nullopt)};
    pre_fader.connections = {{NodeId{1}, NodeId{2}, SendTap::kPreFader, FaderId{1}}};
    EXPECT_EQ(build_error(pre_fader), GraphError::kInvalidSendTap);

    GraphDescription too_late;
    too_late.nodes = {NodeSpec::make_plugin(
        NodeId{1}, std::make_shared<FakePluginProcessor>(PluginBusLayout{0, 2},
                                                         kMaxLatencyCompensationSamples + 1))};
    EXPECT_EQ(build_error(too_late), GraphError::kLatencyOutOfRange);
}

}  // namespace
}  // namespace vocroz::audio_core
