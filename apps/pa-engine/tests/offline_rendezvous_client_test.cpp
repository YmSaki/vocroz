// SPDX-License-Identifier: AGPL-3.0-only
//
// OfflineRendezvousClient の単体テスト。

#include "offline_rendezvous_client.h"

#include <chrono>

#include <gtest/gtest.h>

namespace vocroz::engine {
namespace {

using std::chrono_literals::operator""ms;

// 登録の結果は呼び出しの中ではなくループで返す（session の中から session を呼び返さない）。
TEST(OfflineRendezvousClientTest, ReportsUnreachableLaterOnTheLoop) {
    ControlLoop loop;
    OfflineRendezvousClient client(loop);
    std::optional<control_protocol::ErrorCode> error;
    session::InviteToken received_token{};
    client.set_registration_handler(
        [&](const session::InviteToken& token,
            const common::Result<session::RegisteredInvite, control_protocol::ErrorCode>& result) {
            received_token = token;
            error = result.error();
            loop.request_stop();
        });
    session::InviteRegistration registration;
    registration.invite_token.fill(7);
    client.register_invite(registration);
    EXPECT_FALSE(error.has_value());
    loop.run(1000ms, [] {});
    EXPECT_EQ(error, control_protocol::ErrorCode::kUnreachable);
    EXPECT_EQ(received_token, registration.invite_token);
}

}  // namespace
}  // namespace vocroz::engine
