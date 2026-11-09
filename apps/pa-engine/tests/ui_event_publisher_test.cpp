// SPDX-License-Identifier: AGPL-3.0-only
//
// UiEventPublisher の単体テスト。

#include "ui_event_publisher.h"

#include <gtest/gtest.h>

#include "vocroz/control_protocol/json_codec.h"

#include "fakes/fake_logger.h"
#include "fakes/fake_message_channel.h"

namespace vocroz::engine {
namespace {

namespace cp = control_protocol;

TEST(UiEventPublisherTest, SendsEncodedMessages) {
    cp::FakeMessageChannel channel;
    common::FakeLogger logger;
    UiEventPublisher publisher(channel, logger);
    const cp::EngineToUiMessage message = cp::Meters{{cp::MeterLevel{"master", -6.0}}};
    publisher.publish(message);
    ASSERT_EQ(channel.sent().size(), 1U);
    const auto decoded = cp::decode_engine_to_ui(channel.sent().front());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), message);
}

// 起動直後は UI がつながっていない。送れたらつながっている、閉じていたらつながっていない。
TEST(UiEventPublisherTest, TracksUiPresenceFromSendResults) {
    cp::FakeMessageChannel channel;
    common::FakeLogger logger;
    UiEventPublisher publisher(channel, logger);
    EXPECT_FALSE(publisher.is_ui_connected());
    publisher.publish(cp::Meters{});
    EXPECT_TRUE(publisher.is_ui_connected());
    channel.close();
    publisher.publish(cp::Meters{});
    EXPECT_FALSE(publisher.is_ui_connected());
    publisher.note_ui_message_received();
    EXPECT_TRUE(publisher.is_ui_connected());
}

}  // namespace
}  // namespace vocroz::engine
