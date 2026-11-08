// SPDX-License-Identifier: AGPL-3.0-only
//
// 名前付きパイプのサーバーとクライアントの単体テスト（Windows だけ）。
// 実際のパイプを作り、往復、切断と再接続、ACL、不正なフレームの扱いを確かめる。

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// windows.h は aclapi.h と sddl.h より先に読み込む（後の 2 つが windows.h の型を使うため）。
// clang-format off
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
// clang-format on
#include <gtest/gtest.h>

#include "vocroz/control_protocol/constants.h"
#include "vocroz/control_protocol/ports/message_listener.h"
#include "vocroz/ipc/factories.h"
#include "vocroz/ipc/frame_codec.h"

#include "fakes/fake_logger.h"

namespace vocroz::ipc {
namespace {

using namespace std::chrono_literals;
using control_protocol::ChannelError;
using control_protocol::MessageChannel;

constexpr auto kWaitTimeout = 5s;

// テストごとに別の名前を使い、並行して動くテストや前のテストの残りと衝突しないようにする。
std::wstring unique_pipe_name() {
    static std::atomic<int> counter{0};
    return L"\\\\.\\pipe\\vocroz-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
           std::to_wstring(counter.fetch_add(1));
}

// 受け取ったメッセージと切断を記録し、テストのスレッドから待てるようにするリスナー。
class WaitingListener final : public control_protocol::MessageListener {
public:
    void on_message(std::string_view message) override {
        {
            const std::lock_guard lock{mutex_};
            messages_.emplace_back(message);
        }
        changed_.notify_all();
    }

    void on_closed() override {
        {
            const std::lock_guard lock{mutex_};
            ++closed_;
        }
        changed_.notify_all();
    }

    /// count 件のメッセージが届くまで待ち、届いたメッセージを返す。
    std::vector<std::string> wait_messages(std::size_t count) {
        std::unique_lock lock{mutex_};
        changed_.wait_for(lock, kWaitTimeout, [&] { return messages_.size() >= count; });
        return messages_;
    }

    /// on_closed() が呼ばれるまで待つ。呼ばれたら true。
    bool wait_closed(std::chrono::milliseconds timeout = kWaitTimeout) {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, timeout, [&] { return closed_ > 0; });
    }

    int closed_count() {
        const std::lock_guard lock{mutex_};
        return closed_;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<std::string> messages_;
    int closed_ = 0;
};

class NamedPipeTest : public ::testing::Test {
protected:
    std::unique_ptr<MessageChannel> make_server() {
        auto server = make_pipe_server(name_, logger_);
        EXPECT_TRUE(server.has_value());
        return server ? std::move(server).value() : nullptr;
    }

    std::unique_ptr<MessageChannel> connect_client(std::chrono::milliseconds timeout = 2000ms) {
        auto client = connect_pipe_client(name_, timeout, logger_);
        EXPECT_TRUE(client.has_value());
        return client ? std::move(client).value() : nullptr;
    }

    const std::wstring name_ = unique_pipe_name();
    common::FakeLogger logger_;
};

TEST_F(NamedPipeTest, RoundTripsMessagesInBothDirections) {
    auto server = make_server();
    WaitingListener engine;
    server->set_listener(&engine);
    auto client = connect_client();
    WaitingListener ui;
    client->set_listener(&ui);

    ASSERT_TRUE(client->send(R"({"type":"hello"})").has_value());
    ASSERT_TRUE(client->send(R"({"type":"set_gain"})").has_value());
    EXPECT_EQ(engine.wait_messages(2),
              (std::vector<std::string>{R"({"type":"hello"})", R"({"type":"set_gain"})"}));

    ASSERT_TRUE(server->send(R"({"type":"mixer_snapshot"})").has_value());
    EXPECT_EQ(ui.wait_messages(1), std::vector<std::string>{R"({"type":"mixer_snapshot"})"});

    server->set_listener(nullptr);
    client->set_listener(nullptr);
}

TEST_F(NamedPipeTest, CarriesLargestMessageAndUtf8) {
    auto server = make_server();
    WaitingListener engine;
    server->set_listener(&engine);
    auto client = connect_client();

    const std::string largest(control_protocol::kMaxMessageBytes, 'x');
    const std::string japanese = R"({"name":"ボーカル"})";
    ASSERT_TRUE(client->send(largest).has_value());
    ASSERT_TRUE(client->send(japanese).has_value());
    const auto received = engine.wait_messages(2);
    ASSERT_EQ(received.size(), 2U);
    EXPECT_EQ(received[0], largest);
    EXPECT_EQ(received[1], japanese);

    server->set_listener(nullptr);
}

TEST_F(NamedPipeTest, RejectsMessageLargerThanLimitOnSend) {
    auto server = make_server();
    auto client = connect_client();
    const std::string too_large(control_protocol::kMaxMessageBytes + 1, 'x');
    const auto sent = client->send(too_large);
    ASSERT_FALSE(sent.has_value());
    EXPECT_EQ(sent.error(), ChannelError::kTooLarge);
}

TEST_F(NamedPipeTest, ServerSendFailsWhileNoUiIsConnected) {
    auto server = make_server();
    const auto sent = server->send("{}");
    ASSERT_FALSE(sent.has_value());
    EXPECT_EQ(sent.error(), ChannelError::kClosed);
}

TEST_F(NamedPipeTest, ServerAcceptsTheNextUiAfterDisconnect) {
    auto server = make_server();
    WaitingListener engine;
    server->set_listener(&engine);

    {
        auto first = connect_client();
        ASSERT_TRUE(first->send("first").has_value());
        engine.wait_messages(1);
    }
    // UI の切断はエンジンに on_closed() で知らせない。
    auto second = connect_client();
    WaitingListener ui;
    second->set_listener(&ui);
    ASSERT_TRUE(second->send("second").has_value());
    EXPECT_EQ(engine.wait_messages(2), (std::vector<std::string>{"first", "second"}));
    EXPECT_EQ(engine.closed_count(), 0);

    ASSERT_TRUE(server->send("to second").has_value());
    EXPECT_EQ(ui.wait_messages(1), std::vector<std::string>{"to second"});

    server->set_listener(nullptr);
    second->set_listener(nullptr);
}

TEST_F(NamedPipeTest, ClientReportsClosedOnceWhenServerStops) {
    auto server = make_server();
    WaitingListener engine;
    server->set_listener(&engine);
    auto client = connect_client();
    WaitingListener ui;
    client->set_listener(&ui);
    ASSERT_TRUE(client->send("hello").has_value());
    engine.wait_messages(1);

    server->set_listener(nullptr);
    server.reset();
    EXPECT_TRUE(ui.wait_closed());
    EXPECT_EQ(ui.closed_count(), 1);

    const auto sent = client->send("after close");
    ASSERT_FALSE(sent.has_value());
    EXPECT_EQ(sent.error(), ChannelError::kClosed);
    client->set_listener(nullptr);
}

TEST_F(NamedPipeTest, ClientDoesNotReportClosedWhenDestroyedByOwner) {
    auto server = make_server();
    auto client = connect_client();
    WaitingListener ui;
    client->set_listener(&ui);
    client.reset();
    EXPECT_EQ(ui.closed_count(), 0);
}

TEST_F(NamedPipeTest, SecondServerWithTheSameNameFails) {
    auto server = make_server();
    const auto second = make_pipe_server(name_, logger_);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error(), PipeError::kAlreadyExists);
}

TEST_F(NamedPipeTest, ConnectTimesOutWithoutServer) {
    const auto started = std::chrono::steady_clock::now();
    const auto client = connect_pipe_client(name_, 200ms, logger_);
    ASSERT_FALSE(client.has_value());
    EXPECT_EQ(client.error(), PipeError::kNotFound);
    EXPECT_GE(std::chrono::steady_clock::now() - started, 200ms);
}

TEST_F(NamedPipeTest, ConnectWaitsForServerThatStartsLater) {
    std::unique_ptr<MessageChannel> server;
    std::thread starter{[&] {
        std::this_thread::sleep_for(200ms);
        server = make_server();
    }};
    auto client = connect_pipe_client(name_, 5000ms, logger_);
    starter.join();
    EXPECT_TRUE(client.has_value());
}

TEST_F(NamedPipeTest, SecondUiIsBusyWhileTheFirstIsConnected) {
    auto server = make_server();
    auto first = connect_client();
    const auto second = connect_pipe_client(name_, 200ms, logger_);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error(), PipeError::kNotFound);
}

TEST_F(NamedPipeTest, ServerDropsUiThatSendsOversizedFrameAndAcceptsTheNext) {
    auto server = make_server();
    WaitingListener engine;
    server->set_listener(&engine);

    // MessageChannel を通さずにパイプを開き、上限を超える長さを書き込む。
    HANDLE raw = CreateFileW(name_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                             0, nullptr);
    ASSERT_NE(raw, INVALID_HANDLE_VALUE);
    const std::string header("\xff\xff\xff\x7f", 4);
    DWORD written = 0;
    ASSERT_TRUE(WriteFile(raw, header.data(), 4, &written, nullptr));
    // サーバーが切断すると、こちらの読み込みは失敗する。
    char byte = 0;
    DWORD read = 0;
    EXPECT_FALSE(ReadFile(raw, &byte, 1, &read, nullptr));
    CloseHandle(raw);

    auto client = connect_client();
    ASSERT_TRUE(client->send("valid").has_value());
    EXPECT_EQ(engine.wait_messages(1), std::vector<std::string>{"valid"});
    server->set_listener(nullptr);
}

TEST_F(NamedPipeTest, ServerDropsUiThatStopsReadingInsteadOfBlocking) {
    auto server = make_server();
    WaitingListener engine;
    server->set_listener(&engine);

    // 読み込みをしない UI（ハングした UI の代わり）。hello だけを送り、接続を確立させる。
    HANDLE raw = CreateFileW(name_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                             0, nullptr);
    ASSERT_NE(raw, INVALID_HANDLE_VALUE);
    const std::string hello = encode_frame("hello").value();
    DWORD written = 0;
    ASSERT_TRUE(WriteFile(raw, hello.data(), static_cast<DWORD>(hello.size()), &written, nullptr));
    engine.wait_messages(1);

    // send() はブロックせず、未送信が上限を超えたところで接続を切って kClosed を返す。
    const std::string large(control_protocol::kMaxMessageBytes, 'm');
    bool closed = false;
    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < 64 && !closed; ++i) {
        const auto sent = server->send(large);
        closed = !sent.has_value() && sent.error() == ChannelError::kClosed;
    }
    EXPECT_TRUE(closed);
    EXPECT_LT(std::chrono::steady_clock::now() - started, 2s);
    CloseHandle(raw);

    // 切った後は次の UI を受け付ける。
    auto client = connect_client();
    ASSERT_TRUE(client->send("next").has_value());
    EXPECT_EQ(engine.wait_messages(2), (std::vector<std::string>{"hello", "next"}));
    server->set_listener(nullptr);
}

// ACL を外から読み、所有者と許可がログイン中ユーザーだけであることを確かめる。
// ほかのユーザーでの接続は CI で試せないので、記述子の形で確かめる。
TEST_F(NamedPipeTest, PipeIsOwnedAndAccessibleOnlyByTheCurrentUser) {
    auto server = make_server();

    HANDLE raw_token = nullptr;
    ASSERT_TRUE(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token));
    DWORD size = 0;
    GetTokenInformation(raw_token, TokenUser, nullptr, 0, &size);
    std::vector<DWORD> token_user((size + sizeof(DWORD) - 1) / sizeof(DWORD));
    ASSERT_TRUE(GetTokenInformation(raw_token, TokenUser, token_user.data(), size, &size));
    CloseHandle(raw_token);
    const PSID user_sid = reinterpret_cast<TOKEN_USER*>(token_user.data())->User.Sid;

    HANDLE pipe = CreateFileW(name_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(pipe, INVALID_HANDLE_VALUE);
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    ASSERT_EQ(GetSecurityInfo(pipe, SE_KERNEL_OBJECT,
                              OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
                              nullptr, &dacl, nullptr, &descriptor),
              static_cast<DWORD>(ERROR_SUCCESS));
    CloseHandle(pipe);

    EXPECT_TRUE(EqualSid(owner, user_sid));
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    ASSERT_TRUE(GetSecurityDescriptorControl(descriptor, &control, &revision));
    EXPECT_NE(control & SE_DACL_PROTECTED, 0) << "DACL が親から継承を受けている";

    ASSERT_NE(dacl, nullptr) << "DACL がない（全員に許可される）";
    ASSERT_EQ(dacl->AceCount, 1);
    void* ace = nullptr;
    ASSERT_TRUE(GetAce(dacl, 0, &ace));
    const auto* header = static_cast<ACE_HEADER*>(ace);
    ASSERT_EQ(header->AceType, ACCESS_ALLOWED_ACE_TYPE);
    const auto* allowed = static_cast<ACCESS_ALLOWED_ACE*>(ace);
    EXPECT_TRUE(EqualSid(const_cast<DWORD*>(&allowed->SidStart), user_sid));
    LocalFree(descriptor);
}

TEST_F(NamedPipeTest, EnginePipeNameIsStableForTheSameDataDir) {
    const auto a = engine_pipe_name(L"C:\\VOCROZ\\data");
    const auto b = engine_pipe_name(L"c:/vocroz/data/");
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(a.value(), b.value());
    EXPECT_EQ(a.value().rfind(L"\\\\.\\pipe\\vocroz-engine-S-", 0), 0U);
}

}  // namespace
}  // namespace vocroz::ipc
