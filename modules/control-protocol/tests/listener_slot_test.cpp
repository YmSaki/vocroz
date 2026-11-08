// SPDX-License-Identifier: AGPL-3.0-only
//
// MessageListenerSlot の単体テスト。

#include "vocroz/control_protocol/listener_slot.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::control_protocol {
namespace {

class RecordingListener final : public MessageListener {
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

// 呼び出しの中で、解除を試みる別スレッドが待たされていることを確かめるためのリスナー。
class BlockingListener final : public MessageListener {
public:
    void on_message(std::string_view /*message*/) override {
        entered = true;
        while (!release) {
            std::this_thread::yield();
        }
        finished = true;
    }
    void on_closed() override {}

    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    std::atomic<bool> finished{false};
};

TEST(MessageListenerSlotTest, DeliversToTheCurrentListener) {
    MessageListenerSlot slot;
    RecordingListener listener;
    slot.set(&listener);

    slot.deliver_message(R"({"type":"hello"})");
    slot.deliver_closed();

    EXPECT_EQ(listener.messages, std::vector<std::string>{R"({"type":"hello"})"});
    EXPECT_EQ(listener.closed, 1);
}

TEST(MessageListenerSlotTest, DropsMessagesWithoutListener) {
    MessageListenerSlot slot;
    RecordingListener listener;
    slot.set(&listener);
    slot.set(nullptr);

    slot.deliver_message("{}");
    slot.deliver_closed();

    EXPECT_TRUE(listener.messages.empty());
    EXPECT_EQ(listener.closed, 0);
}

TEST(MessageListenerSlotTest, SetWaitsUntilTheRunningCallbackReturns) {
    MessageListenerSlot slot;
    BlockingListener listener;
    slot.set(&listener);

    std::thread receiver{[&] { slot.deliver_message("{}"); }};
    while (!listener.entered) {
        std::this_thread::yield();
    }
    std::atomic<bool> unset{false};
    std::thread owner{[&] {
        slot.set(nullptr);
        unset = true;
    }};
    // 呼び出しが終わるまで解除は戻らない。
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    EXPECT_FALSE(unset);

    listener.release = true;
    owner.join();
    receiver.join();
    EXPECT_TRUE(listener.finished);
    EXPECT_TRUE(unset);
}

}  // namespace
}  // namespace vocroz::control_protocol
