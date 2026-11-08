// SPDX-License-Identifier: AGPL-3.0-only
//
// WebView のブリッジの MessageChannel の単体テスト。WebView の代わりに、届けたメッセージを記録する
// UiEventSink を使う。

#include "bridge_channel.h"

#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/control_protocol/constants.h"

namespace vocroz::webui {
namespace {

using control_protocol::ChannelError;
using control_protocol::kMaxMessageBytes;

class RecordingSink final : public UiEventSink {
public:
    void post_to_ui(std::string message) override {
        const std::lock_guard lock{mutex_};
        posted_.push_back(std::move(message));
    }

    std::vector<std::string> posted() {
        const std::lock_guard lock{mutex_};
        return posted_;
    }

private:
    std::mutex mutex_;
    std::vector<std::string> posted_;
};

class RecordingListener final : public control_protocol::MessageListener {
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

TEST(BridgeChannelTest, SendPostsMessageToUiUnchanged) {
    RecordingSink sink;
    BridgeChannel bridge{sink};
    ASSERT_TRUE(bridge.send(R"({"type":"mixer_snapshot"})").has_value());
    EXPECT_EQ(sink.posted(), std::vector<std::string>{R"({"type":"mixer_snapshot"})"});
}

TEST(BridgeChannelTest, SendRejectsMessageLargerThanLimit) {
    RecordingSink sink;
    BridgeChannel bridge{sink};
    const auto sent = bridge.send(std::string(kMaxMessageBytes + 1, 'x'));
    ASSERT_FALSE(sent.has_value());
    EXPECT_EQ(sent.error(), ChannelError::kTooLarge);
    EXPECT_TRUE(sink.posted().empty());
}

TEST(BridgeChannelTest, SendFailsAfterCloseWithoutTouchingSink) {
    RecordingSink sink;
    BridgeChannel bridge{sink};
    bridge.close();
    const auto sent = bridge.send("{}");
    ASSERT_FALSE(sent.has_value());
    EXPECT_EQ(sent.error(), ChannelError::kClosed);
    EXPECT_TRUE(sink.posted().empty());
}

TEST(BridgeChannelTest, ReceiveFromUiDeliversToListener) {
    RecordingSink sink;
    BridgeChannel bridge{sink};
    RecordingListener listener;
    bridge.set_listener(&listener);

    EXPECT_TRUE(bridge.receive_from_ui(R"({"type":"hello"})"));
    EXPECT_EQ(listener.messages, std::vector<std::string>{R"({"type":"hello"})"});
    EXPECT_EQ(listener.closed, 0);
    bridge.set_listener(nullptr);
}

TEST(BridgeChannelTest, ReceiveFromUiDropsMessageLargerThanLimit) {
    RecordingSink sink;
    BridgeChannel bridge{sink};
    RecordingListener listener;
    bridge.set_listener(&listener);

    EXPECT_FALSE(bridge.receive_from_ui(std::string(kMaxMessageBytes + 1, 'x')));
    EXPECT_TRUE(listener.messages.empty());
    bridge.set_listener(nullptr);
}

TEST(BridgeChannelTest, CloseDoesNotNotifyListener) {
    RecordingSink sink;
    BridgeChannel bridge{sink};
    RecordingListener listener;
    bridge.set_listener(&listener);
    bridge.close();
    EXPECT_EQ(listener.closed, 0);
    bridge.set_listener(nullptr);
}

TEST(BridgeChannelTest, KeepsOrderOfSendsFromOneThreadWhileOthersSend) {
    RecordingSink sink;
    BridgeChannel bridge{sink};
    std::thread other{[&] {
        for (int i = 0; i < 100; ++i) {
            (void)bridge.send("other");
        }
    }};
    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(bridge.send(std::to_string(i)).has_value());
    }
    other.join();

    int expected = 0;
    for (const auto& message : sink.posted()) {
        if (message != "other") {
            EXPECT_EQ(message, std::to_string(expected));
            ++expected;
        }
    }
    EXPECT_EQ(expected, 100);
}

}  // namespace
}  // namespace vocroz::webui
