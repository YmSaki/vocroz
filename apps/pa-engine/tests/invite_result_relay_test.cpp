// SPDX-License-Identifier: AGPL-3.0-only
//
// InviteResultRelay の単体テスト。作業用のスレッドから届いた結果を、コントロールスレッドの
// ループで session に渡すことを確かめる。

#include "invite_result_relay.h"

#include <chrono>
#include <optional>
#include <thread>

#include <gtest/gtest.h>

namespace vocroz::engine {
namespace {

using std::chrono_literals::operator""ms;

TEST(InviteResultRelayTest, DeliversRegistrationOnTheLoopThread) {
    ControlLoop loop;
    InviteResultRelay relay(loop);
    std::optional<std::thread::id> called_on;
    std::optional<control_protocol::ErrorCode> error;
    session::InviteToken received{};
    relay.set_registration_handler(
        [&](const session::InviteToken& token,
            const common::Result<session::RegisteredInvite, control_protocol::ErrorCode>& result) {
            called_on = std::this_thread::get_id();
            received = token;
            error = result.error();
            loop.request_stop();
        });

    session::InviteToken token{};
    token.fill(3);
    std::thread worker([&] {
        relay.on_invite_registered(
            token, common::make_unexpected(control_protocol::ErrorCode::kUnreachable));
    });
    worker.join();
    // 作業用のスレッドの中では呼ばない。
    EXPECT_FALSE(called_on.has_value());

    loop.run(1000ms, [] {});
    EXPECT_EQ(called_on, std::this_thread::get_id());
    EXPECT_EQ(received, token);
    EXPECT_EQ(error, control_protocol::ErrorCode::kUnreachable);
}

}  // namespace
}  // namespace vocroz::engine
