// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の参加者の一覧のテスト。

#include "participant_registry.h"

#include <gtest/gtest.h>

namespace vocroz::session {
namespace {

using control_protocol::ConnectionState;

ReconnectToken token(std::uint8_t value) {
    ReconnectToken t{};
    t.fill(value);
    return t;
}

TEST(ParticipantRegistryTest, NewStripsStartMutedAndConnectingInApprovalOrder) {
    ParticipantRegistry registry;
    const auto& first = registry.add_pending(10, "Vo");
    EXPECT_EQ(first.entry.entity_id, "pending1");
    EXPECT_EQ(first.entry.short_id, "#01");
    registry.add_pending(11, "Gt");

    const auto second = *registry.find_pending("pending2");
    auto& strip = registry.add_participant(second, token(2)).strip;
    EXPECT_EQ(strip.entity_id, "ch1");
    EXPECT_EQ(strip.participant_id, "p1");
    EXPECT_EQ(strip.name, "Gt");
    EXPECT_EQ(strip.short_id, "#02");
    EXPECT_EQ(strip.connection_state, ConnectionState::kConnecting);
    EXPECT_TRUE(strip.channel_muted);
    EXPECT_TRUE(strip.send_muted);
    EXPECT_EQ(registry.find_by_peer(11), &registry.participants().front());
}

TEST(ParticipantRegistryTest, FindsParticipantsByReconnectToken) {
    ParticipantRegistry registry;
    registry.add_participant(registry.add_pending(1, "A"), token(1));
    registry.add_participant(registry.add_pending(2, "B"), token(2));
    ASSERT_NE(registry.find_by_token(token(2)), nullptr);
    EXPECT_EQ(registry.find_by_token(token(2))->strip.name, "B");
    EXPECT_EQ(registry.find_by_token(token(3)), nullptr);
    registry.remove("ch2");
    // ストリップを削除したら、再接続トークンも無効になる。
    EXPECT_EQ(registry.find_by_token(token(2)), nullptr);
}

TEST(ParticipantRegistryTest, NeverReusesEntityIdsWithinASession) {
    ParticipantRegistry registry;
    registry.add_participant(registry.add_pending(1, "A"), token(1));
    registry.remove("ch1");
    EXPECT_EQ(registry.add_participant(registry.add_pending(2, "B"), token(2)).strip.entity_id,
              "ch2");
}

TEST(ParticipantRegistryTest, IsActiveOnlyWhileSomeoneIsConnectedOrReconnecting) {
    ParticipantRegistry registry;
    auto& p = registry.add_participant(registry.add_pending(1, "A"), token(1));
    EXPECT_FALSE(registry.any_active());
    p.strip.connection_state = ConnectionState::kConnected;
    EXPECT_TRUE(registry.any_active());
    p.strip.connection_state = ConnectionState::kReconnecting;
    EXPECT_TRUE(registry.any_active());
    p.strip.connection_state = ConnectionState::kLost;
    EXPECT_FALSE(registry.any_active());
}

}  // namespace
}  // namespace vocroz::session
