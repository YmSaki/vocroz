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

TEST(MixerGraphBuilderTest, MakesStripStereoWhenItsSourceIsStereo) {
    MixerAssignments a = two_strips();
    a.strips[1].source =
        std::make_shared<ac::FakeAudioSource>(ac::FakeAudioSource::stereo(0.1F, 0.2F));
    const auto built = build_mixer_graph(a, kMaster, cp::RoutingConfig{}, false);

    EXPECT_EQ(node_with_fader(built.graph, ac::FaderId{1})->channels, 1U);
    EXPECT_EQ(node_with_fader(built.graph, ac::FaderId{2})->channels, 2U);
    EXPECT_TRUE(is_valid(built.graph));
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

}  // namespace
}  // namespace vocroz::engine
