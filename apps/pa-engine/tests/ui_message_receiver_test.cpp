// SPDX-License-Identifier: AGPL-3.0-only
//
// UiMessageReceiver の単体テスト。

#include "ui_message_receiver.h"

#include <chrono>
#include <thread>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_logger.h"

namespace vocroz::engine {
namespace {

using std::chrono_literals::operator""ms;

// 受信スレッドで受けたメッセージが、ループのスレッドで handler に渡る。
TEST(UiMessageReceiverTest, DeliversDecodedMessagesOnTheLoopThread) {
    ControlLoop loop;
    common::FakeLogger logger;
    std::vector<control_protocol::UiToEngineMessage> received;
    std::thread::id handler_thread;
    UiMessageReceiver receiver(
        loop,
        [&](const control_protocol::UiToEngineMessage& message) {
            received.push_back(message);
            handler_thread = std::this_thread::get_id();
            loop.request_stop();
        },
        logger);
    std::thread pipe_thread([&] { receiver.on_message(R"({"type":"hello"})"); });
    pipe_thread.join();
    loop.run(1000ms, [] {});
    ASSERT_EQ(received.size(), 1U);
    EXPECT_TRUE(std::holds_alternative<control_protocol::Hello>(received.front()));
    EXPECT_EQ(handler_thread, std::this_thread::get_id());
}

// 不正なメッセージは session に渡さず、値を含めずにログに残す。
TEST(UiMessageReceiverTest, DropsInvalidMessages) {
    ControlLoop loop;
    common::FakeLogger logger;
    bool called = false;
    UiMessageReceiver receiver(
        loop, [&](const control_protocol::UiToEngineMessage&) { called = true; }, logger);
    receiver.on_message(R"({"type":"set_gain","entity_id":"secret-value"})");
    receiver.on_message("not json");
    loop.post([&] { loop.request_stop(); });
    loop.run(1000ms, [] {});
    EXPECT_FALSE(called);
    ASSERT_EQ(logger.entries().size(), 2U);
    for (const auto& entry : logger.entries()) {
        EXPECT_EQ(entry.level, common::LogLevel::kWarning);
        EXPECT_EQ(entry.message.find("secret-value"), std::string::npos);
    }
}

}  // namespace
}  // namespace vocroz::engine
