// SPDX-License-Identifier: AGPL-3.0-only
//
// SessionActivityTap のテスト。エンジンのスナップショットとセッションのイベントから稼働中かを読み、
// メッセージはそのまま中継することを確かめる。

#include "session_activity_tap.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/control_protocol/json_codec.h"

#include "fakes/fake_message_channel.h"

namespace vocroz::pa_desktop {
namespace {

namespace cp = control_protocol;

class RecordingListener final : public cp::MessageListener {
public:
    void on_message(std::string_view message) override {
        messages.emplace_back(message);
    }
    void on_closed() override {
        ++closed;
    }
    std::vector<std::string> messages;
    int closed = 0;
};

std::string session_updated(cp::SessionState state) {
    cp::SessionUpdated event;
    event.session.entity_id = "session";
    event.session.state = state;
    return cp::encode_engine_to_ui(event);
}

std::string snapshot(cp::SessionState state) {
    cp::MixerSnapshotEvent event;
    event.snapshot.session.entity_id = "session";
    event.snapshot.session.state = state;
    event.snapshot.master.entity_id = "master";
    event.snapshot.routing.entity_id = "routing";
    return cp::encode_engine_to_ui(event);
}

TEST(SessionActivityTapTest, FollowsTheSessionStateFromTheEngine) {
    cp::FakeMessageChannel engine;
    SessionActivityTap tap(engine);
    RecordingListener listener;
    tap.set_listener(&listener);
    EXPECT_FALSE(tap.session_active());

    engine.deliver(snapshot(cp::SessionState::kActive));
    EXPECT_TRUE(tap.session_active());
    engine.deliver(session_updated(cp::SessionState::kReady));
    EXPECT_FALSE(tap.session_active());
    engine.deliver(session_updated(cp::SessionState::kActive));
    EXPECT_TRUE(tap.session_active());
    EXPECT_EQ(listener.messages.size(), 3U);
}

TEST(SessionActivityTapTest, RelaysEveryMessageUnchanged) {
    cp::FakeMessageChannel engine;
    SessionActivityTap tap(engine);
    RecordingListener listener;
    tap.set_listener(&listener);

    engine.deliver("not json");
    const auto meters = cp::encode_engine_to_ui(cp::Meters{});
    engine.deliver(meters);
    ASSERT_EQ(listener.messages.size(), 2U);
    EXPECT_EQ(listener.messages[0], "not json");
    EXPECT_EQ(listener.messages[1], meters);
    EXPECT_FALSE(tap.session_active());

    ASSERT_TRUE(tap.send("{\"type\":\"hello\"}"));
    ASSERT_EQ(engine.sent().size(), 1U);
    EXPECT_EQ(engine.sent()[0], "{\"type\":\"hello\"}");
}

TEST(SessionActivityTapTest, ClosedEngineIsNotActive) {
    cp::FakeMessageChannel engine;
    SessionActivityTap tap(engine);
    RecordingListener listener;
    tap.set_listener(&listener);
    engine.deliver(snapshot(cp::SessionState::kActive));
    engine.close();
    EXPECT_FALSE(tap.session_active());
    EXPECT_EQ(listener.closed, 1);
}

TEST(SessionActivityTapTest, DetachedListenerReceivesNothing) {
    cp::FakeMessageChannel engine;
    SessionActivityTap tap(engine);
    RecordingListener listener;
    tap.set_listener(&listener);
    tap.set_listener(nullptr);
    engine.deliver(session_updated(cp::SessionState::kActive));
    EXPECT_TRUE(listener.messages.empty());
}

}  // namespace
}  // namespace vocroz::pa_desktop
