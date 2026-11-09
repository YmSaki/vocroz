// SPDX-License-Identifier: AGPL-3.0-only
//
// ルーティングの構成の検証のテスト。ID、参照、つなげる組み合わせ、値の範囲、ループの判定と、
// ルーティング画面に返す理由と entity_id を確かめる。

#include "routing_validator.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "session_constants.h"

namespace vocroz::session {
namespace {

namespace cp = control_protocol;
using cp::RoutingIssue;
using cp::SendTap;

const std::vector<std::string> kStrips{"ch1", "ch2"};

cp::RoutingSend send(const std::string& id, const std::string& from, const std::string& to,
                     SendTap tap = SendTap::kPostFader) {
    return cp::RoutingSend{id, from, to, tap};
}

/// キューバスにプリフェーダーで送り、追加の出力から出す構成。
cp::RoutingConfig monitor_config() {
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-cue", "返し", cp::BusKind::kCue});
    config.outputs.push_back(cp::RoutingOutput{"out-mon", "モニター", 2});
    config.sends.push_back(send("send-1", "ch1", "bus-cue", SendTap::kPreFader));
    config.sends.push_back(send("send-2", "bus-cue", "out-mon"));
    config.input_delays.push_back(cp::InputDelay{"ch2", 20.0});
    return config;
}

void expect_issue(const cp::RoutingConfig& config, RoutingIssue issue,
                  const std::optional<std::string>& entity_id) {
    const auto check = check_routing(config, kStrips);
    EXPECT_EQ(check.issue, issue);
    EXPECT_EQ(check.issue_entity_id, entity_id);
    EXPECT_FALSE(check.needs_approval);
}

TEST(RoutingValidatorTest, AcceptsAnEmptyConfig) {
    const auto check = check_routing({}, kStrips);
    EXPECT_EQ(check, cp::RoutingCheck{});
}

TEST(RoutingValidatorTest, AcceptsAPreFaderCueSendToAnExtraOutput) {
    const auto check = check_routing(monitor_config(), kStrips);
    EXPECT_EQ(check, cp::RoutingCheck{});
}

TEST(RoutingValidatorTest, RejectsIdsWithoutTheirKindPrefix) {
    auto config = monitor_config();
    config.buses[0].entity_id = "cue";
    expect_issue(config, RoutingIssue::kInvalidId, "cue");

    config = monitor_config();
    config.outputs[0].entity_id = "out-";
    expect_issue(config, RoutingIssue::kInvalidId, "out-");

    // エンジンが振る ID とは接頭辞で区別するので、ストリップと同じ形の ID も使えない。
    config = monitor_config();
    config.sends[0].entity_id = "ch3";
    expect_issue(config, RoutingIssue::kInvalidId, "ch3");
}

TEST(RoutingValidatorTest, RejectsDuplicateIds) {
    auto config = monitor_config();
    config.buses.push_back(cp::RoutingBus{"bus-cue", "返し 2", cp::BusKind::kCue});
    expect_issue(config, RoutingIssue::kDuplicateId, "bus-cue");

    config = monitor_config();
    config.sends.push_back(send("send-1", "ch2", "bus-cue"));
    expect_issue(config, RoutingIssue::kDuplicateId, "send-1");

    config = monitor_config();
    config.input_delays.push_back(cp::InputDelay{"ch2", 1.0});
    expect_issue(config, RoutingIssue::kDuplicateId, "ch2");
}

TEST(RoutingValidatorTest, RejectsMoreItemsThanTheLimit) {
    cp::RoutingConfig config;
    for (std::size_t i = 0; i <= kMaxRoutingDelays; ++i) {
        config.delays.push_back(cp::RoutingDelay{"delay-" + std::to_string(i), 100.0});
    }
    expect_issue(config, RoutingIssue::kTooManyItems, std::nullopt);
}

TEST(RoutingValidatorTest, RejectsReferencesToUnknownNodes) {
    auto config = monitor_config();
    config.sends.push_back(send("send-3", "ch9", "bus-cue"));
    expect_issue(config, RoutingIssue::kUnknownNode, "send-3");

    config = monitor_config();
    config.input_delays.push_back(cp::InputDelay{"ch9", 1.0});
    expect_issue(config, RoutingIssue::kUnknownNode, "ch9");

    // 手動ディレイはストリップにだけ掛けられる。
    config = monitor_config();
    config.input_delays.push_back(cp::InputDelay{"bus-cue", 1.0});
    expect_issue(config, RoutingIssue::kUnknownNode, "bus-cue");
}

TEST(RoutingValidatorTest, RejectsSendsThatCannotBeConnected) {
    const std::vector<cp::RoutingSend> invalid{
        send("send-9", "bus-cue", "ch1"),      // ストリップへの入力
        send("send-9", "out-mon", "bus-cue"),  // 出力からの送り
        send("send-9", "bus-cue", "bus-cue"),  // 自分自身
        send("send-9", "ch1", "master"),       // 暗黙の結線との重複
        send("send-9", "ch1", "bus-cue"),      // 同じ組の重複
    };
    for (const auto& s : invalid) {
        auto config = monitor_config();
        config.sends.push_back(s);
        SCOPED_TRACE(s.from + " -> " + s.to);
        expect_issue(config, RoutingIssue::kInvalidSend, "send-9");
    }
}

TEST(RoutingValidatorTest, RejectsPreFaderSendsFromNodesWithoutAFader) {
    auto config = monitor_config();
    config.sends.push_back(send("send-3", "master", "out-mon", SendTap::kPreFader));
    config.sends.erase(config.sends.begin() + 1);
    expect_issue(config, RoutingIssue::kInvalidSendTap, "send-3");
}

TEST(RoutingValidatorTest, RejectsDelaysOutOfRange) {
    // 遅延ノードは 1 ブロック（128 サンプル、約 2.67 ms）より短くできない。
    cp::RoutingConfig config;
    config.delays.push_back(cp::RoutingDelay{"delay-1", 2.0});
    expect_issue(config, RoutingIssue::kDelayOutOfRange, "delay-1");
    config.delays[0].delay_ms = 2.7;
    EXPECT_FALSE(check_routing(config, kStrips).issue);
    config.delays[0].delay_ms = 2000.0;
    EXPECT_FALSE(check_routing(config, kStrips).issue);

    // 手動ディレイは 1 秒まで。
    config = monitor_config();
    config.input_delays[0].delay_ms = 1000.0;
    EXPECT_FALSE(check_routing(config, kStrips).issue);
    config.input_delays[0].delay_ms = 1000.1;
    expect_issue(config, RoutingIssue::kDelayOutOfRange, "ch2");
}

TEST(RoutingValidatorTest, RejectsOutputChannelsOutOfRangeOrOverlapping) {
    auto config = monitor_config();
    config.outputs[0].device_channel = 62;
    EXPECT_FALSE(check_routing(config, kStrips).issue);
    config.outputs[0].device_channel = 63;
    expect_issue(config, RoutingIssue::kOutputChannelOutOfRange, "out-mon");

    // メインの出力はチャンネル 0 と 1 を使う。
    config.outputs[0].device_channel = 1;
    expect_issue(config, RoutingIssue::kOverlappingOutputs, "out-mon");

    config = monitor_config();
    config.outputs.push_back(cp::RoutingOutput{"out-2", "返し 2", 3});
    expect_issue(config, RoutingIssue::kOverlappingOutputs, "out-2");
    config.outputs[1].device_channel = 4;
    EXPECT_FALSE(check_routing(config, kStrips).issue);
}

TEST(RoutingValidatorTest, RejectsLoopsWithoutADelayNodeAndShowsTheirNodes) {
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-fx", "FX", cp::BusKind::kUser});
    config.sends.push_back(send("send-1", "master", "bus-fx"));
    config.sends.push_back(send("send-2", "bus-fx", "master"));
    const auto check = check_routing(config, kStrips);
    EXPECT_EQ(check.issue, RoutingIssue::kUndelayedLoop);
    EXPECT_EQ(check.looped_entity_ids, (std::vector<std::string>{"bus-fx", "master"}));
    EXPECT_EQ(check.issue_entity_id, "bus-fx");
    EXPECT_FALSE(check.needs_approval);
}

TEST(RoutingValidatorTest, LoopsThroughADelayNodeNeedApproval) {
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-fx", "FX", cp::BusKind::kUser});
    config.delays.push_back(cp::RoutingDelay{"delay-echo", 250.0});
    config.sends.push_back(send("send-1", "master", "delay-echo"));
    config.sends.push_back(send("send-2", "delay-echo", "bus-fx"));
    config.sends.push_back(send("send-3", "bus-fx", "master"));
    const auto check = check_routing(config, kStrips);
    EXPECT_FALSE(check.issue);
    EXPECT_TRUE(check.needs_approval);
    EXPECT_EQ(check.looped_entity_ids,
              (std::vector<std::string>{"bus-fx", "delay-echo", "master"}));
}

}  // namespace
}  // namespace vocroz::session
