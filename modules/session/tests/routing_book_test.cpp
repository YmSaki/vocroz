// SPDX-License-Identifier: AGPL-3.0-only
//
// ルーティングの状態の遷移のテスト。ステージングと適用の分離、revision の進め方、ループの承認、
// 新しいバスとセンドの初期ミュート、ストリップの削除の反映を確かめる。

#include "routing_book.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::session {
namespace {

namespace cp = control_protocol;
using cp::ErrorCode;

const std::vector<std::string> kStrips{"ch1", "ch2"};

cp::RoutingConfig cue_config() {
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-cue", "返し", cp::BusKind::kCue});
    config.sends.push_back(cp::RoutingSend{"send-1", "ch1", "bus-cue", cp::SendTap::kPreFader});
    config.sends.push_back(cp::RoutingSend{"send-2", "ch2", "bus-cue", cp::SendTap::kPreFader});
    config.input_delays.push_back(cp::InputDelay{"ch1", 10.0});
    return config;
}

cp::RoutingConfig echo_config() {
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-fx", "FX", cp::BusKind::kUser});
    config.delays.push_back(cp::RoutingDelay{"delay-echo", 250.0});
    config.sends.push_back(
        cp::RoutingSend{"send-1", "master", "delay-echo", cp::SendTap::kPostFader});
    config.sends.push_back(
        cp::RoutingSend{"send-2", "delay-echo", "bus-fx", cp::SendTap::kPostFader});
    config.sends.push_back(cp::RoutingSend{"send-3", "bus-fx", "master", cp::SendTap::kPostFader});
    return config;
}

TEST(RoutingBookTest, StartsWithAnEmptyRoutingAndNoStaging) {
    const RoutingBook book;
    EXPECT_EQ(book.info().entity_id, "routing");
    EXPECT_EQ(book.info().revision, 1U);
    EXPECT_EQ(book.info().applied, cp::RoutingConfig{});
    EXPECT_FALSE(book.info().staged);
    EXPECT_TRUE(book.info().levels.empty());
}

TEST(RoutingBookTest, StagingKeepsTheAppliedRoutingAndAdvancesTheRevision) {
    RoutingBook book;
    book.stage(cue_config(), kStrips);
    EXPECT_EQ(book.info().revision, 2U);
    EXPECT_EQ(book.info().applied, cp::RoutingConfig{});
    ASSERT_TRUE(book.info().staged);
    EXPECT_EQ(book.info().staged->config, cue_config());
    EXPECT_FALSE(book.info().staged->check.issue);
}

TEST(RoutingBookTest, StagesAnInvalidConfigWithItsIssue) {
    RoutingBook book;
    auto config = cue_config();
    config.sends[0].to = "bus-missing";
    book.stage(config, kStrips);
    ASSERT_TRUE(book.info().staged);
    EXPECT_EQ(book.info().staged->check.issue, cp::RoutingIssue::kUnknownNode);
    EXPECT_EQ(book.prepare_commit(true, kStrips).error(), ErrorCode::kInvalidState);
}

TEST(RoutingBookTest, CommitAppliesTheStagedConfigWithNewLevelsMuted) {
    RoutingBook book;
    book.stage(cue_config(), kStrips);
    ASSERT_TRUE(book.prepare_commit(false, kStrips));
    book.commit();
    EXPECT_EQ(book.info().revision, 3U);
    EXPECT_EQ(book.info().applied, cue_config());
    EXPECT_FALSE(book.info().staged);
    EXPECT_FALSE(book.info().feedback_approved);
    ASSERT_EQ(book.info().levels.size(), 3U);
    EXPECT_EQ(book.info().levels[0], (cp::RouteLevel{"bus-cue", 1, 0.0, true}));
    EXPECT_EQ(book.info().levels[1].entity_id, "send-1");
    EXPECT_EQ(book.info().levels[2].entity_id, "send-2");
}

TEST(RoutingBookTest, CommitKeepsTheLevelsOfEntitiesThatRemain) {
    RoutingBook book;
    book.stage(cue_config(), kStrips);
    ASSERT_TRUE(book.prepare_commit(false, kStrips));
    book.commit();
    auto* level = book.find_level("send-1");
    ASSERT_NE(level, nullptr);
    *level = cp::RouteLevel{"send-1", 4, -6.0, false};

    auto config = cue_config();
    config.sends.pop_back();
    book.stage(config, kStrips);
    ASSERT_TRUE(book.prepare_commit(false, kStrips));
    book.commit();
    ASSERT_NE(book.find_level("send-1"), nullptr);
    EXPECT_EQ(*book.find_level("send-1"), (cp::RouteLevel{"send-1", 4, -6.0, false}));
    EXPECT_EQ(book.find_level("send-2"), nullptr);
}

TEST(RoutingBookTest, LoopsThroughADelayNodeAreAppliedOnlyWithApproval) {
    RoutingBook book;
    book.stage(echo_config(), kStrips);
    EXPECT_EQ(book.prepare_commit(false, kStrips).error(), ErrorCode::kInvalidState);
    ASSERT_TRUE(book.prepare_commit(true, kStrips));
    book.commit();
    EXPECT_TRUE(book.info().feedback_approved);
}

TEST(RoutingBookTest, CommitAndDiscardNeedAStagedConfig) {
    RoutingBook book;
    EXPECT_EQ(book.prepare_commit(true, kStrips).error(), ErrorCode::kInvalidState);
    EXPECT_EQ(book.discard().error(), ErrorCode::kInvalidState);
    book.stage(cue_config(), kStrips);
    ASSERT_TRUE(book.discard());
    EXPECT_FALSE(book.info().staged);
    EXPECT_EQ(book.info().revision, 3U);
}

TEST(RoutingBookTest, RevalidatesTheStagedConfigWhenAStripIsRemoved) {
    RoutingBook book;
    book.stage(cue_config(), kStrips);
    const std::vector<std::string> remaining{"ch2"};
    EXPECT_FALSE(book.forget_strip("ch1", remaining));
    EXPECT_EQ(book.info().revision, 2U);
    EXPECT_TRUE(book.info().staged->check.issue);
    EXPECT_EQ(book.prepare_commit(false, remaining).error(), ErrorCode::kInvalidState);
}

TEST(RoutingBookTest, RemovingAStripDropsItsSendsAndInputDelayFromTheAppliedRouting) {
    RoutingBook book;
    book.stage(cue_config(), kStrips);
    ASSERT_TRUE(book.prepare_commit(false, kStrips));
    book.commit();
    EXPECT_TRUE(book.forget_strip("ch1", std::vector<std::string>{"ch2"}));
    EXPECT_EQ(book.info().revision, 4U);
    ASSERT_EQ(book.info().applied.sends.size(), 1U);
    EXPECT_EQ(book.info().applied.sends[0].entity_id, "send-2");
    EXPECT_TRUE(book.info().applied.input_delays.empty());
    EXPECT_EQ(book.find_level("send-1"), nullptr);
    EXPECT_NE(book.find_level("bus-cue"), nullptr);
}

TEST(WithoutStripReferencesTest, KeepsOnlyRoutesBetweenMasterBusesOutputsAndDelays) {
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-a", "A", cp::BusKind::kUser});
    config.outputs.push_back(cp::RoutingOutput{"out-a", "A", 2});
    config.delays.push_back(cp::RoutingDelay{"delay-a", 100.0});
    config.sends.push_back(cp::RoutingSend{"send-1", "ch1", "bus-a", cp::SendTap::kPreFader});
    config.sends.push_back(cp::RoutingSend{"send-2", "bus-a", "out-a", cp::SendTap::kPostFader});
    config.sends.push_back(cp::RoutingSend{"send-3", "master", "delay-a", cp::SendTap::kPostFader});
    config.sends.push_back(cp::RoutingSend{"send-4", "delay-a", "ch2", cp::SendTap::kPostFader});
    config.input_delays.push_back(cp::InputDelay{"ch1", 5.0});
    config.inserts.push_back(cp::RoutingInsert{"fx-1", "ch1", "eq"});
    config.inserts.push_back(cp::RoutingInsert{"fx-2", "master", "comp"});
    config.inserts.push_back(cp::RoutingInsert{"fx-3", "bus-a", "verb"});

    const auto kept = without_strip_references(config);
    EXPECT_EQ(kept.buses, config.buses);
    EXPECT_EQ(kept.outputs, config.outputs);
    EXPECT_EQ(kept.delays, config.delays);
    ASSERT_EQ(kept.sends.size(), 2U);
    EXPECT_EQ(kept.sends[0].entity_id, "send-2");
    EXPECT_EQ(kept.sends[1].entity_id, "send-3");
    EXPECT_TRUE(kept.input_delays.empty());
    ASSERT_EQ(kept.inserts.size(), 2U);
    EXPECT_EQ(kept.inserts[0].entity_id, "fx-2");
    EXPECT_EQ(kept.inserts[1].entity_id, "fx-3");
}

TEST(RoutingBookTest, RemovingAStripDropsItsInsertsAndRevalidatesWithThePluginList) {
    PluginDescription eq;
    eq.identifier = "eq";
    RoutingBook book;
    book.set_plugins({eq}, kStrips);
    auto config = cue_config();
    config.inserts.push_back(cp::RoutingInsert{"fx-1", "ch1", "eq"});
    book.stage(config, kStrips);
    ASSERT_TRUE(book.prepare_commit(false, kStrips));
    book.commit();
    EXPECT_TRUE(book.forget_strip("ch1", std::vector<std::string>{"ch2"}));
    EXPECT_TRUE(book.info().applied.inserts.empty());

    // 一覧からプラグインが消えると、ステージング中の挿入は適用できなくなる。
    book.stage(config, kStrips);
    EXPECT_FALSE(book.info().staged->check.issue);
    const auto revision = book.info().revision;
    book.set_plugins({}, kStrips);
    EXPECT_EQ(book.info().staged->check.issue, cp::RoutingIssue::kUnknownPlugin);
    EXPECT_EQ(book.info().revision, revision);
}

}  // namespace
}  // namespace vocroz::session
