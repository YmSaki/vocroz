// SPDX-License-Identifier: AGPL-3.0-only
//
// build_mixer_graph() の単体テスト。暗黙の経路（ストリップ → Master → リミッター → 出力）、
// センドの結線、参照先のないセンドの除外を確かめる。組み立てたグラフは Mixer の検証も通す。

#include "mixer_graph_builder.h"

#include <algorithm>
#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include "vocroz/audio_core/mixer.h"
#include "vocroz/session/routing_delay.h"

#include "fakes/fake_audio_sink.h"
#include "fakes/fake_audio_source.h"

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
    const auto built = build_mixer_graph(two_strips(), kMaster, cp::RoutingConfig{}, false);
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

TEST(MixerGraphBuilderTest, AppliesInputDelayOnlyToMatchingStrip) {
    cp::RoutingConfig config;
    config.input_delays.push_back(cp::InputDelay{"ch-2", 10.0});
    config.input_delays.push_back(cp::InputDelay{"missing", 5.0});

    const auto built = build_mixer_graph(two_strips(), kMaster, config, false);

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

    const auto built = build_mixer_graph(assignments, kMaster, config, false);
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

    const auto built = build_mixer_graph(assignments, kMaster, config, false);

    EXPECT_EQ(built.dropped_sends, (std::vector<std::string>{"send-gone", "send-nofader"}));
    EXPECT_TRUE(std::none_of(built.graph.connections.begin(), built.graph.connections.end(),
                             [](const ac::Connection& c) { return c.level.has_value(); }));
    EXPECT_TRUE(is_valid(built.graph));
}

TEST(MixerGraphBuilderTest, SkipsBusWithoutFaderAndCarriesFeedbackApproval) {
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-unassigned"});

    const auto built = build_mixer_graph(two_strips(), kMaster, config, true);

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
    const auto built = build_mixer_graph(two_strips(), kMaster, cp::RoutingConfig{}, false);
    EXPECT_EQ(count_kind(built.graph, ac::NodeKind::kReturnTap), 0U);
}

TEST(MixerGraphBuilderTest, ReturnCarriesTheMainMixWithoutTheOwnStrip) {
    auto a = two_strips();
    auto sink1 = std::make_shared<ac::FakeAudioSink>();
    auto sink2 = std::make_shared<ac::FakeAudioSink>();
    a.strips[0].return_sink = sink1;
    a.strips[1].return_sink = sink2;
    const auto built = build_mixer_graph(a, kMaster, cp::RoutingConfig{}, false);
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
    a.strips[0].return_sink = sink1;
    a.strips[1].return_sink = sink2;
    // ch-1 → bus-1 → Master（リバーブなどのバス）。
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-1", "Rev"});
    config.sends.push_back(cp::RoutingSend{"send-1", "ch-1", "bus-1", cp::SendTap::kPostFader});
    config.sends.push_back(cp::RoutingSend{"send-2", "bus-1", "master", cp::SendTap::kPostFader});
    const auto built = build_mixer_graph(a, kMaster, config, false);
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

}  // namespace
}  // namespace vocroz::engine
