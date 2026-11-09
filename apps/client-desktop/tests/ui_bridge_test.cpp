// SPDX-License-Identifier: AGPL-3.0-only
//
// UI と session の間の変換（UiBridge）のテスト。

#include "ui_bridge.h"

#include <vector>

#include <gtest/gtest.h>

#include "vocroz/control_protocol/json_codec.h"

#include "fakes/fake_logger.h"
#include "fakes/fake_message_channel.h"

namespace vocroz::client {
namespace {
namespace cp = control_protocol;

class UiBridgeTest : public ::testing::Test {
protected:
    cp::FakeMessageChannel channel_;
    common::FakeLogger logger_;
    UiBridge bridge_{channel_, logger_};
    std::vector<cp::UiToEngineMessage> received_;
};

TEST_F(UiBridgeTest, DecodesUiMessagesForSession) {
    bridge_.start([this](const cp::UiToEngineMessage& m) { received_.push_back(m); });
    channel_.deliver(cp::encode_ui_to_engine(cp::Hello{}));

    ASSERT_EQ(received_.size(), 1U);
    EXPECT_TRUE(std::holds_alternative<cp::Hello>(received_[0]));
}

TEST_F(UiBridgeTest, DropsMalformedMessages) {
    bridge_.start([this](const cp::UiToEngineMessage& m) { received_.push_back(m); });
    channel_.deliver("{not json");

    EXPECT_TRUE(received_.empty());
    EXPECT_FALSE(logger_.entries().empty());
}

TEST_F(UiBridgeTest, EncodesSessionMessagesForUi) {
    bridge_.publish(
        cp::Notice{.severity = cp::NoticeSeverity::kInfo, .code = "joined", .params = {}});

    const auto sent = channel_.sent();
    ASSERT_EQ(sent.size(), 1U);
    const auto decoded = cp::decode_engine_to_ui(sent[0]);
    ASSERT_TRUE(decoded);
    EXPECT_TRUE(std::holds_alternative<cp::Notice>(decoded.value()));
}

TEST_F(UiBridgeTest, DetachesListenerOnDestruction) {
    {
        UiBridge scoped{channel_, logger_};
        scoped.start([](const cp::UiToEngineMessage&) {});
        EXPECT_EQ(channel_.current_listener(), &scoped);
    }
    EXPECT_EQ(channel_.current_listener(), nullptr);
}

}  // namespace
}  // namespace vocroz::client
