// SPDX-License-Identifier: AGPL-3.0-only
//
// ChannelRelay の単体テスト。

#include "vocroz/control_protocol/channel_relay.h"

#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_message_channel.h"

namespace vocroz::control_protocol {
namespace {

using Side = ChannelRelay::Side;

TEST(ChannelRelayTest, ForwardsMessagesInBothDirectionsUnchanged) {
    FakeMessageChannel ui;
    FakeMessageChannel engine;
    const ChannelRelay relay{ui, engine, {}};

    // 中継はデコードしないので、JSON として不正な文字列もそのまま渡す（検証はエンジンが行う）。
    ui.deliver(R"({"type":"hello"})");
    ui.deliver("not json");
    engine.deliver(R"({"type":"ack","request_id":"r1","applied_revision":2})");

    EXPECT_EQ(engine.sent(), (std::vector<std::string>{R"({"type":"hello"})", "not json"}));
    EXPECT_EQ(
        ui.sent(),
        (std::vector<std::string>{R"({"type":"ack","request_id":"r1","applied_revision":2})"}));
}

TEST(ChannelRelayTest, ReportsWhichSideClosed) {
    FakeMessageChannel ui;
    FakeMessageChannel engine;
    std::vector<Side> closed;
    const ChannelRelay relay{ui, engine, [&closed](Side side) { closed.push_back(side); }};

    engine.close();
    ui.close();

    EXPECT_EQ(closed, (std::vector<Side>{Side::kEngine, Side::kUi}));
}

TEST(ChannelRelayTest, DropsMessagesWhenDestinationIsClosed) {
    FakeMessageChannel ui;
    FakeMessageChannel engine;
    const ChannelRelay relay{ui, engine, {}};

    engine.close();
    ui.deliver(R"({"type":"hello"})");

    EXPECT_TRUE(engine.sent().empty());
}

TEST(ChannelRelayTest, DetachesListenersOnDestruction) {
    FakeMessageChannel ui;
    FakeMessageChannel engine;
    {
        const ChannelRelay relay{ui, engine, {}};
        EXPECT_NE(ui.current_listener(), nullptr);
        EXPECT_NE(engine.current_listener(), nullptr);
    }
    EXPECT_EQ(ui.current_listener(), nullptr);
    EXPECT_EQ(engine.current_listener(), nullptr);

    ui.deliver(R"({"type":"hello"})");
    EXPECT_TRUE(engine.sent().empty());
}

}  // namespace
}  // namespace vocroz::control_protocol
