// SPDX-License-Identifier: AGPL-3.0-only
//
// build_mixer_graph() の単体テスト。暗黙の経路（ストリップ → Master → リミッター → 出力）、
// センドの結線、参照先のないセンドの除外、挿入の置き場所と読み込み前の経路の切断を確かめる。組み立てたグラフは
// Mixer の検証も通す。

#include "mixer_graph_builder.h"

#include <algorithm>
#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include "vocroz/audio_core/mixer.h"
#include "vocroz/session/routing_delay.h"

#include "fakes/fake_audio_sink.h"
#include "fakes/fake_audio_source.h"
#include "fakes/fake_audio_stream_sink.h"
#include "fakes/fake_midi_source.h"
#include "fakes/fake_plugin_processor.h"

namespace vocroz::engine {
namespace {

namespace ac = audio_core;
namespace cp = control_protocol;

constexpr char kMaster[] = "master";

MixerAssignments two_strips() {
    MixerAssignments a;
    a.master_fader = ac::FaderId{0};
    a.master_meter = ac::MeterId{0};
    a.strips.push_back(StripAssignment{
        "ch-1", ac::FaderId{1}, ac::MeterId{1},
        std::make_shared<ac::FakeAudioSource>(ac::FakeAudioSource::constant(0.1F))});
    a.strips.push_back(StripAssignment{"ch-2", ac::FaderId{2}, ac::MeterId{2}, nullptr});
    return a;
}

std::size_t count_kind(const ac::GraphDescription& g, ac::NodeKind kind) {
    return static_cast<std::size_t>(std::count_if(
        g.nodes.begin(), g.nodes.end(), [kind](const ac::NodeSpec& n) { return n.kind == kind; }));
}

const ac::NodeSpec* node_with_fader(const ac::GraphDescription& g, ac::FaderId fader) {
    const auto it = std::find_if(g.nodes.begin(), g.nodes.end(),
                                 [fader](const ac::NodeSpec& n) { return n.fader == fader; });
    return it == g.nodes.end() ? nullptr : &*it;
}

bool connected(const ac::GraphDescription& g, ac::NodeId from, ac::NodeId to) {
    return std::any_of(g.connections.begin(), g.connections.end(),
                       [&](const ac::Connection& c) { return c.from == from && c.to == to; });
}

bool is_valid(const ac::GraphDescription& g) {
    ac::Mixer mixer;
    return mixer.validate_graph(g).has_value();
}

TEST(MixerGraphBuilderTest, BuildsImplicitPathFromStripsThroughMasterAndLimiter) {
    const auto built = build_mixer_graph(two_strips(), kMaster, cp::RoutingConfig{}, false, {});
    const auto& g = built.graph;

    EXPECT_TRUE(built.dropped_sends.empty());
    EXPECT_EQ(count_kind(g, ac::NodeKind::kStrip), 2U);
    // ソースがあるのは ch-1 だけ。ch-2 はストリップだけを置いて無音にする。
    EXPECT_EQ(count_kind(g, ac::NodeKind::kSource), 1U);
    EXPECT_EQ(count_kind(g, ac::NodeKind::kLimiter), 1U);
    EXPECT_EQ(count_kind(g, ac::NodeKind::kOutput), 1U);

    const auto* master = node_with_fader(g, ac::FaderId{0});
    const auto* ch1 = node_with_fader(g, ac::FaderId{1});
    const auto* ch2 = node_with_fader(g, ac::FaderId{2});
    ASSERT_NE(master, nullptr);
    ASSERT_NE(ch1, nullptr);
    ASSERT_NE(ch2, nullptr);
    EXPECT_EQ(master->kind, ac::NodeKind::kBus);
    EXPECT_TRUE(connected(g, ch1->id, master->id));
    EXPECT_TRUE(connected(g, ch2->id, master->id));

    const auto output = std::find_if(g.nodes.begin(), g.nodes.end(), [](const ac::NodeSpec& n) {
        return n.kind == ac::NodeKind::kOutput;
    });
    EXPECT_EQ(output->device_channel, 0U);
    EXPECT_EQ(output->meter, ac::MeterId{0});
    EXPECT_TRUE(is_valid(g));
}

TEST(MixerGraphBuilderTest, MakesStripStereoWhenItsSourceIsStereo) {
    MixerAssignments a = two_strips();
    a.strips[1].source =
        std::make_shared<ac::FakeAudioSource>(ac::FakeAudioSource::stereo(0.1F, 0.2F));
    const auto built = build_mixer_graph(a, kMaster, cp::RoutingConfig{}, false, {});

    EXPECT_EQ(node_with_fader(built.graph, ac::FaderId{1})->channels, 1U);
    EXPECT_EQ(node_with_fader(built.graph, ac::FaderId{2})->channels, 2U);
    EXPECT_TRUE(is_valid(built.graph));
}

TEST(MixerGraphBuilderTest, AppliesInputDelayOnlyToMatchingStrip) {
    cp::RoutingConfig config;
    config.input_delays.push_back(cp::InputDelay{"ch-2", 10.0});
    config.input_delays.push_back(cp::InputDelay{"missing", 5.0});

    const auto built = build_mixer_graph(two_strips(), kMaster, config, false, {});

    EXPECT_EQ(node_with_fader(built.graph, ac::FaderId{1})->delay_samples, 0U);
    EXPECT_EQ(node_with_fader(built.graph, ac::FaderId{2})->delay_samples,
              session::delay_samples(10.0));
    EXPECT_TRUE(is_valid(built.graph));
}

TEST(MixerGraphBuilderTest, ConnectsSendsWithLevelAndPutsLimiterBeforeExtraOutput) {
    auto assignments = two_strips();
    assignments.route_faders.emplace("bus-1", ac::FaderId{10});
    assignments.route_faders.emplace("send-1", ac::FaderId{11});
    assignments.route_faders.emplace("send-2", ac::FaderId{12});
    assignments.bus_meters.emplace("bus-1", ac::MeterId{10});
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-1"});
    config.outputs.push_back(cp::RoutingOutput{"out-1", "Cue", 2});
    config.sends.push_back(cp::RoutingSend{"send-1", "ch-1", "bus-1", cp::SendTap::kPreFader});
    config.sends.push_back(cp::RoutingSend{"send-2", "bus-1", "out-1", cp::SendTap::kPostFader});

    const auto built = build_mixer_graph(assignments, kMaster, config, false, {});
    const auto& g = built.graph;

    EXPECT_TRUE(built.dropped_sends.empty());
    // メインの出力と追加の出力のそれぞれにリミッターを置く。
    EXPECT_EQ(count_kind(g, ac::NodeKind::kLimiter), 2U);
    EXPECT_EQ(count_kind(g, ac::NodeKind::kOutput), 2U);
    const auto send1 =
        std::find_if(g.connections.begin(), g.connections.end(),
                     [](const ac::Connection& c) { return c.level == ac::FaderId{11}; });
    ASSERT_NE(send1, g.connections.end());
    EXPECT_EQ(send1->tap, ac::SendTap::kPreFader);
    EXPECT_EQ(send1->from, node_with_fader(g, ac::FaderId{1})->id);
    EXPECT_EQ(send1->to, node_with_fader(g, ac::FaderId{10})->id);

    const auto send2 =
        std::find_if(g.connections.begin(), g.connections.end(),
                     [](const ac::Connection& c) { return c.level == ac::FaderId{12}; });
    ASSERT_NE(send2, g.connections.end());
    const auto target = std::find_if(g.nodes.begin(), g.nodes.end(),
                                     [&](const ac::NodeSpec& n) { return n.id == send2->to; });
    EXPECT_EQ(target->kind, ac::NodeKind::kLimiter);
    EXPECT_TRUE(is_valid(g));
}

TEST(MixerGraphBuilderTest, DropsSendsWithMissingEndpointsOrFader) {
    auto assignments = two_strips();
    assignments.route_faders.emplace("send-gone", ac::FaderId{11});
    cp::RoutingConfig config;
    // 送り元のストリップが削除済み。
    config.sends.push_back(cp::RoutingSend{"send-gone", "ch-9", kMaster, cp::SendTap::kPostFader});
    // フェーダーが割り当てられていない。
    config.sends.push_back(
        cp::RoutingSend{"send-nofader", "ch-1", kMaster, cp::SendTap::kPreFader});

    const auto built = build_mixer_graph(assignments, kMaster, config, false, {});

    EXPECT_EQ(built.dropped_sends, (std::vector<std::string>{"send-gone", "send-nofader"}));
    EXPECT_TRUE(std::none_of(built.graph.connections.begin(), built.graph.connections.end(),
                             [](const ac::Connection& c) { return c.level.has_value(); }));
    EXPECT_TRUE(is_valid(built.graph));
}

TEST(MixerGraphBuilderTest, SkipsBusWithoutFaderAndCarriesFeedbackApproval) {
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-unassigned"});

    const auto built = build_mixer_graph(two_strips(), kMaster, config, true, {});

    // Master だけがバス。
    EXPECT_EQ(count_kind(built.graph, ac::NodeKind::kBus), 1U);
    EXPECT_TRUE(built.graph.feedback_approved);
}

// 返送タップのうち、sink に渡すもの。
const ac::NodeSpec* return_tap_to(const ac::GraphDescription& g,
                                  const std::shared_ptr<ac::AudioSink>& sink) {
    const auto it = std::find_if(g.nodes.begin(), g.nodes.end(), [&](const ac::NodeSpec& n) {
        return n.kind == ac::NodeKind::kReturnTap && n.sink == sink;
    });
    return it == g.nodes.end() ? nullptr : &*it;
}

// 返送タップの手前のリミッターへ入る送り元。
std::vector<ac::NodeId> return_inputs(const ac::GraphDescription& g, ac::NodeId tap) {
    ac::NodeId limiter{};
    for (const auto& c : g.connections) {
        if (c.to == tap) {
            limiter = c.from;
        }
    }
    std::vector<ac::NodeId> inputs;
    for (const auto& c : g.connections) {
        if (c.to == limiter) {
            inputs.push_back(c.from);
        }
    }
    return inputs;
}

TEST(MixerGraphBuilderTest, BuildsNoReturnWithoutReturnSinks) {
    const auto built = build_mixer_graph(two_strips(), kMaster, cp::RoutingConfig{}, false, {});
    EXPECT_EQ(count_kind(built.graph, ac::NodeKind::kReturnTap), 0U);
}

TEST(MixerGraphBuilderTest, ReturnCarriesTheMainMixWithoutTheOwnStrip) {
    auto a = two_strips();
    auto sink1 = std::make_shared<ac::FakeAudioSink>();
    auto sink2 = std::make_shared<ac::FakeAudioSink>();
    a.returns.push_back({1, {"ch-1"}, sink1});
    a.returns.push_back({2, {"ch-2"}, sink2});
    const auto built = build_mixer_graph(a, kMaster, cp::RoutingConfig{}, false, {});
    const auto& g = built.graph;
    const auto* ch1 = node_with_fader(g, ac::FaderId{1});
    const auto* ch2 = node_with_fader(g, ac::FaderId{2});
    const auto* tap1 = return_tap_to(g, sink1);
    const auto* tap2 = return_tap_to(g, sink2);
    ASSERT_NE(tap1, nullptr);
    ASSERT_NE(tap2, nullptr);
    EXPECT_EQ(return_inputs(g, tap1->id), (std::vector<ac::NodeId>{ch2->id}));
    EXPECT_EQ(return_inputs(g, tap2->id), (std::vector<ac::NodeId>{ch1->id}));
    // 返送の経路にも安全リミッターを通す。
    EXPECT_EQ(count_kind(g, ac::NodeKind::kLimiter), 3U);
    EXPECT_TRUE(is_valid(g));
}

TEST(MixerGraphBuilderTest, ReturnExcludesBusesThatCarryTheOwnVoice) {
    auto a = two_strips();
    a.route_faders.emplace("bus-1", ac::FaderId{3});
    a.route_faders.emplace("send-1", ac::FaderId{4});
    a.route_faders.emplace("send-2", ac::FaderId{5});
    auto sink1 = std::make_shared<ac::FakeAudioSink>();
    auto sink2 = std::make_shared<ac::FakeAudioSink>();
    a.returns.push_back({1, {"ch-1"}, sink1});
    a.returns.push_back({2, {"ch-2"}, sink2});
    // ch-1 → bus-1 → Master（リバーブなどのバス）。
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-1", "Rev"});
    config.sends.push_back(cp::RoutingSend{"send-1", "ch-1", "bus-1", cp::SendTap::kPostFader});
    config.sends.push_back(cp::RoutingSend{"send-2", "bus-1", "master", cp::SendTap::kPostFader});
    const auto built = build_mixer_graph(a, kMaster, config, false, {});
    const auto& g = built.graph;
    const auto* ch1 = node_with_fader(g, ac::FaderId{1});
    const auto* ch2 = node_with_fader(g, ac::FaderId{2});
    const auto* bus = node_with_fader(g, ac::FaderId{3});
    // ch-1 の返送には、ch-1 の声を含むバスも入らない。
    EXPECT_EQ(return_inputs(g, return_tap_to(g, sink1)->id), (std::vector<ac::NodeId>{ch2->id}));
    // ch-2 の返送には、ch-1 とバスの両方が入る。
    auto inputs2 = return_inputs(g, return_tap_to(g, sink2)->id);
    std::sort(inputs2.begin(), inputs2.end());
    auto expected2 = std::vector<ac::NodeId>{ch1->id, bus->id};
    std::sort(expected2.begin(), expected2.end());
    EXPECT_EQ(inputs2, expected2);
    EXPECT_TRUE(is_valid(g));
}

TEST(MixerGraphBuilderTest, ReturnOfAClientWithSeveralInputsExcludesAllOfItsStrips) {
    // ch-1 と ch-2 は同じ子機のマイクとアプリ音声。ch-1
    // しか送っていない別の子機の返送には両方が入る。
    auto a = two_strips();
    auto own = std::make_shared<ac::FakeAudioSink>();
    a.returns.push_back({1, {"ch-1", "ch-2"}, own});
    const auto built = build_mixer_graph(a, kMaster, cp::RoutingConfig{}, false, {});
    const auto& g = built.graph;
    EXPECT_TRUE(return_inputs(g, return_tap_to(g, own)->id).empty());
    EXPECT_TRUE(is_valid(g));
}

TEST(MixerGraphBuilderTest, ReturnIgnoresOwnStripsThatNoLongerExist) {
    auto a = two_strips();
    auto sink = std::make_shared<ac::FakeAudioSink>();
    a.returns.push_back({1, {"ch-1", "ch-gone"}, sink});
    const auto built = build_mixer_graph(a, kMaster, cp::RoutingConfig{}, false, {});
    const auto& g = built.graph;
    const auto* ch2 = node_with_fader(g, ac::FaderId{2});
    EXPECT_EQ(return_inputs(g, return_tap_to(g, sink)->id), (std::vector<ac::NodeId>{ch2->id}));
}

TEST(MixerGraphBuilderTest, MidiSourcesKeepTheirIdsAndHaveNoConnectionsYet) {
    auto a = two_strips();
    auto device = std::make_shared<ac::FakeMidiSource>();
    auto client = std::make_shared<ac::FakeMidiSource>();
    a.midi_sources.push_back({"device:a", ac::MidiSourceId{7}, device});
    a.midi_sources.push_back({"strip:ch-1", ac::MidiSourceId{3}, client});
    const auto built = build_mixer_graph(a, kMaster, cp::RoutingConfig{}, false, {});
    const auto& sources = built.graph.midi_sources;
    ASSERT_EQ(sources.size(), 2U);
    EXPECT_EQ(sources[0].id, ac::MidiSourceId{7});
    EXPECT_EQ(sources[0].source, device);
    EXPECT_EQ(sources[1].id, ac::MidiSourceId{3});
    EXPECT_EQ(sources[1].source, client);
    // 送り先のプラグインノードはまだグラフにないので、経路は作らない。
    EXPECT_TRUE(built.graph.midi_connections.empty());
    EXPECT_TRUE(is_valid(built.graph));
}

// ストリームタップと、その手前のリミッターへ入る送り元。
struct StreamTapPath {
    const ac::NodeSpec* tap = nullptr;
    std::vector<ac::NodeId> sources;
};

StreamTapPath stream_tap_path(const ac::GraphDescription& g) {
    StreamTapPath path;
    const auto it = std::find_if(g.nodes.begin(), g.nodes.end(), [](const ac::NodeSpec& n) {
        return n.kind == ac::NodeKind::kStreamTap;
    });
    if (it == g.nodes.end()) {
        return path;
    }
    path.tap = &*it;
    path.sources = return_inputs(g, it->id);
    return path;
}

TEST(MixerGraphBuilderTest, ExternalStreamTapsTheMasterThroughALimiter) {
    auto a = two_strips();
    const auto sink = std::make_shared<ac::FakeAudioStreamSink>();
    a.external_stream = ExternalStreamAssignment{kMaster, sink};
    const auto built = build_mixer_graph(a, kMaster, cp::RoutingConfig{}, false, {});
    const auto path = stream_tap_path(built.graph);
    ASSERT_NE(path.tap, nullptr);
    EXPECT_EQ(path.tap->stream_sink, sink);
    const auto* master = node_with_fader(built.graph, ac::FaderId{0});
    ASSERT_NE(master, nullptr);
    EXPECT_EQ(path.sources, std::vector<ac::NodeId>{master->id});
    EXPECT_TRUE(is_valid(built.graph));
}

TEST(MixerGraphBuilderTest, ExternalStreamTapsABusPostFader) {
    auto a = two_strips();
    a.route_faders.emplace("bus-1", ac::FaderId{3});
    a.external_stream =
        ExternalStreamAssignment{"bus-1", std::make_shared<ac::FakeAudioStreamSink>()};
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-1", "配信"});
    const auto built = build_mixer_graph(a, kMaster, config, false, {});
    const auto path = stream_tap_path(built.graph);
    ASSERT_NE(path.tap, nullptr);
    const auto* bus = node_with_fader(built.graph, ac::FaderId{3});
    ASSERT_NE(bus, nullptr);
    EXPECT_EQ(path.sources, std::vector<ac::NodeId>{bus->id});
    EXPECT_TRUE(is_valid(built.graph));
}

TEST(MixerGraphBuilderTest, ExternalStreamIsNotTappedFromMissingBusesOrStrips) {
    for (const char* source : {"bus-missing", "ch-1"}) {
        auto a = two_strips();
        a.external_stream =
            ExternalStreamAssignment{source, std::make_shared<ac::FakeAudioStreamSink>()};
        const auto built = build_mixer_graph(a, kMaster, cp::RoutingConfig{}, false, {});
        EXPECT_EQ(count_kind(built.graph, ac::NodeKind::kStreamTap), 0U) << source;
    }
}

session::InsertedPlugin inserted(const std::string& id, ac::PluginBusLayout layout = {}) {
    return session::InsertedPlugin{id, std::make_shared<ac::FakePluginProcessor>(layout),
                                   std::make_shared<ac::PluginFault>()};
}

const ac::NodeSpec* node_with_plugin(const ac::GraphDescription& g,
                                     const session::InsertedPlugin& plugin) {
    const auto it = std::find_if(g.nodes.begin(), g.nodes.end(), [&](const ac::NodeSpec& n) {
        return n.kind == ac::NodeKind::kPlugin && n.plugin == plugin.processor;
    });
    return it == g.nodes.end() ? nullptr : &*it;
}

std::vector<ac::NodeId> inputs_of(const ac::GraphDescription& g, ac::NodeId node) {
    std::vector<ac::NodeId> inputs;
    for (const auto& c : g.connections) {
        if (c.to == node) {
            inputs.push_back(c.from);
        }
    }
    return inputs;
}

TEST(MixerGraphBuilderTest, StripInsertsSitBetweenTheSourceAndTheStripInOrder) {
    cp::RoutingConfig config;
    config.inserts.push_back(cp::RoutingInsert{"fx-1", "ch-1", "eq"});
    config.inserts.push_back(cp::RoutingInsert{"fx-2", "ch-1", "comp"});
    // 構成の順と、渡す順は関係ない。
    const std::vector<session::InsertedPlugin> plugins{inserted("fx-2"), inserted("fx-1")};
    const auto built = build_mixer_graph(two_strips(), kMaster, config, false, plugins);

    const auto* first = node_with_plugin(built.graph, plugins[1]);
    const auto* second = node_with_plugin(built.graph, plugins[0]);
    const auto* strip = node_with_fader(built.graph, ac::FaderId{1});
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(strip, nullptr);
    EXPECT_EQ(first->plugin_fault, plugins[1].fault);
    const auto source_inputs = inputs_of(built.graph, first->id);
    ASSERT_EQ(source_inputs.size(), 1U);
    EXPECT_EQ(built.graph.nodes[source_inputs[0].value].kind, ac::NodeKind::kSource);
    EXPECT_TRUE(connected(built.graph, first->id, second->id));
    EXPECT_EQ(inputs_of(built.graph, strip->id), std::vector<ac::NodeId>{second->id});
    // ステレオのプラグインの後ろのストリップはステレオにする。
    EXPECT_EQ(strip->channels, 2U);
    EXPECT_TRUE(is_valid(built.graph));
}

TEST(MixerGraphBuilderTest, CutsTheStripWhileAnInsertIsNotLoaded) {
    cp::RoutingConfig config;
    config.inserts.push_back(cp::RoutingInsert{"fx-1", "ch-1", "eq"});
    config.inserts.push_back(cp::RoutingInsert{"fx-2", "ch-1", "comp"});
    const std::vector<session::InsertedPlugin> plugins{inserted("fx-1")};
    const auto built = build_mixer_graph(two_strips(), kMaster, config, false, plugins);

    // 読み込み済みの挿入も置かず、ソースはストリップへつながない（プラグインを通らない音を
    // 出さない）。ソースのノードは受信ストリームから取り出し続けるために残す。
    EXPECT_EQ(count_kind(built.graph, ac::NodeKind::kPlugin), 0U);
    EXPECT_EQ(count_kind(built.graph, ac::NodeKind::kSource), 1U);
    const auto* strip = node_with_fader(built.graph, ac::FaderId{1});
    ASSERT_NE(strip, nullptr);
    EXPECT_TRUE(inputs_of(built.graph, strip->id).empty());
    EXPECT_TRUE(is_valid(built.graph));
}

TEST(MixerGraphBuilderTest, MasterInsertsFeedTheLimiterAndTheSendsFromMaster) {
    auto a = two_strips();
    a.route_faders.emplace("bus-1", ac::FaderId{3});
    a.route_faders.emplace("send-1", ac::FaderId{4});
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-1", "録音"});
    config.sends.push_back(cp::RoutingSend{"send-1", kMaster, "bus-1"});
    config.inserts.push_back(cp::RoutingInsert{"fx-1", kMaster, "comp"});
    const std::vector<session::InsertedPlugin> plugins{inserted("fx-1")};
    const auto built = build_mixer_graph(a, kMaster, config, false, plugins);

    const auto* master = node_with_fader(built.graph, ac::FaderId{0});
    const auto* plugin = node_with_plugin(built.graph, plugins[0]);
    const auto* bus = node_with_fader(built.graph, ac::FaderId{3});
    ASSERT_NE(master, nullptr);
    ASSERT_NE(plugin, nullptr);
    ASSERT_NE(bus, nullptr);
    EXPECT_EQ(inputs_of(built.graph, plugin->id), std::vector<ac::NodeId>{master->id});
    // Master から出る結線は、挿入の列への 1 本だけ。リミッターとセンドは挿入の後ろから取る。
    for (const auto& c : built.graph.connections) {
        if (c.from == master->id) {
            EXPECT_EQ(c.to, plugin->id);
        }
    }
    EXPECT_TRUE(connected(built.graph, plugin->id, bus->id));
    EXPECT_EQ(count_kind(built.graph, ac::NodeKind::kLimiter), 1U);
    EXPECT_TRUE(is_valid(built.graph));
}

TEST(MixerGraphBuilderTest, CutsWhatFollowsABusWhileItsInsertIsNotLoaded) {
    auto a = two_strips();
    a.route_faders.emplace("bus-1", ac::FaderId{3});
    a.route_faders.emplace("send-1", ac::FaderId{4});
    a.route_faders.emplace("send-2", ac::FaderId{5});
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-1", "FX"});
    config.sends.push_back(cp::RoutingSend{"send-1", "ch-1", "bus-1"});
    config.sends.push_back(cp::RoutingSend{"send-2", "bus-1", kMaster});
    config.inserts.push_back(cp::RoutingInsert{"fx-1", "bus-1", "verb"});
    const auto built = build_mixer_graph(a, kMaster, config, false, {});

    // センドは外さない（外すとフェーダーをミュートし、session の状態とずれる）。
    EXPECT_TRUE(built.dropped_sends.empty());
    const auto* bus = node_with_fader(built.graph, ac::FaderId{3});
    ASSERT_NE(bus, nullptr);
    for (const auto& c : built.graph.connections) {
        EXPECT_NE(c.from, bus->id);
        if (c.level == ac::FaderId{5}) {
            // バスの後段のセンドは、入力のない無音の段から取る。
            EXPECT_EQ(built.graph.nodes[c.from.value].kind, ac::NodeKind::kLimiter);
            EXPECT_TRUE(inputs_of(built.graph, c.from).empty());
        }
    }
    EXPECT_TRUE(is_valid(built.graph));
}

}  // namespace
}  // namespace vocroz::engine
