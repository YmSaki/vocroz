// SPDX-License-Identifier: AGPL-3.0-only
//
// ChannelGroup の単体テスト。

#include "vocroz/control_protocol/channel_group.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/control_protocol/channel_relay.h"

#include "fakes/fake_message_channel.h"

namespace vocroz::control_protocol {
namespace {

using Side = ChannelRelay::Side;

TEST(ChannelGroupTest, SendsEngineMessagesToEveryWindowAndForwardsEachWindowToTheEngine) {
    FakeMessageChannel mixer;
    FakeMessageChannel routing;
    FakeMessageChannel engine;
    ChannelGroup windows;
    windows.add(mixer);
    const ChannelRelay relay{windows, engine, {}};
    // 中継を始めた後に開いた窓にも、同じ中継のリスナーが付く。
    windows.add(routing);

    engine.deliver("snapshot");
    mixer.deliver("from-mixer");
    routing.deliver("from-routing");

    EXPECT_EQ(mixer.sent(), std::vector<std::string>{"snapshot"});
    EXPECT_EQ(routing.sent(), std::vector<std::string>{"snapshot"});
    EXPECT_EQ(engine.sent(), (std::vector<std::string>{"from-mixer", "from-routing"}));
}

TEST(ChannelGroupTest, RemovedWindowNoLongerSendsOrReceives) {
    FakeMessageChannel mixer;
    FakeMessageChannel routing;
    FakeMessageChannel engine;
    ChannelGroup windows;
    windows.add(mixer);
    windows.add(routing);
    const ChannelRelay relay{windows, engine, {}};

    windows.remove(routing);
    EXPECT_EQ(routing.current_listener(), nullptr);
    engine.deliver("event");
    routing.deliver("late");

    EXPECT_EQ(mixer.sent(), std::vector<std::string>{"event"});
    EXPECT_TRUE(routing.sent().empty());
    EXPECT_TRUE(engine.sent().empty());
}

TEST(ChannelGroupTest, OneClosedWindowDoesNotStopTheOthers) {
    FakeMessageChannel mixer;
    FakeMessageChannel routing;
    ChannelGroup windows;
    windows.add(mixer);
    windows.add(routing);

    routing.close();
    EXPECT_TRUE(windows.send("event"));
    EXPECT_EQ(mixer.sent(), std::vector<std::string>{"event"});
}

TEST(ChannelGroupTest, FailsWhenNoWindowAcceptsTheMessage) {
    FakeMessageChannel mixer;
    ChannelGroup windows;
    const auto empty = windows.send("event");
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.error(), ChannelError::kClosed);

    windows.add(mixer);
    const auto too_large = windows.send(std::string(kMaxMessageBytes + 1, 'x'));
    ASSERT_FALSE(too_large);
    EXPECT_EQ(too_large.error(), ChannelError::kTooLarge);
}

TEST(ChannelGroupTest, ClearsTheListenersOfItsWindowsWhenDestroyed) {
    FakeMessageChannel mixer;
    FakeMessageChannel engine;
    {
        ChannelGroup windows;
        windows.add(mixer);
        windows.set_listener(nullptr);
        const ChannelRelay relay{windows, engine, {}};
        EXPECT_NE(mixer.current_listener(), nullptr);
    }
    EXPECT_EQ(mixer.current_listener(), nullptr);
}

}  // namespace
}  // namespace vocroz::control_protocol
