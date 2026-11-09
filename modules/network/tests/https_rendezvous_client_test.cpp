// SPDX-License-Identifier: AGPL-3.0-only
//
// HttpsRendezvousClient のテスト。HttpsTransport をフェイクにして、要求の順序と、
// 結果の渡し方（要求した招待トークンや招待コードと一緒に渡すこと）を確かめる。
// クライアントの破棄はキューを送り終えるまで待つので、確認はクライアントを破棄してから行う。

#include "vocroz/network/https_rendezvous_client.h"

#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "fakes/fake_https_transport.h"
#include "fakes/fake_invite_result_listener.h"

namespace vocroz::network {
namespace {

using control_protocol::ErrorCode;
using nlohmann::json;

constexpr const char* kAa32 = "qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqo";
constexpr const char* kEleven16 = "EREREREREREREREREREREQ";

session::InviteCode code_of(std::string_view text) {
    auto code = session::parse_invite_code(text);
    EXPECT_TRUE(code.has_value());
    return std::move(code).value();
}

session::InviteRegistration registration_with_token(std::uint8_t fill) {
    session::InviteRegistration registration{};
    registration.invite_token.fill(fill);
    return registration;
}

class HttpsRendezvousClientTest : public ::testing::Test {
protected:
    FakeHttpsTransport transport_;
    FakeInviteResultListener listener_;
};

TEST_F(HttpsRendezvousClientTest, PassesTheRegisteredInviteWithTheRequestedToken) {
    transport_.push_response(201, json{{"code", "ABCDE12345"}, {"revoke_key", kAa32}}.dump());
    {
        HttpsRendezvousClient client(transport_, listener_);
        client.register_invite(registration_with_token(0x42));
    }
    const auto requests = transport_.requests();
    ASSERT_EQ(requests.size(), 1U);
    EXPECT_EQ(requests[0].path, "/v1/invites");
    const auto registered = listener_.registered();
    ASSERT_EQ(registered.size(), 1U);
    EXPECT_EQ(registered[0].token, registration_with_token(0x42).invite_token);
    ASSERT_TRUE(registered[0].result.has_value());
    EXPECT_EQ(registered[0].result.value().code, code_of("ABCDE12345"));
}

TEST_F(HttpsRendezvousClientTest, ReportsAnUnreachableServerAsUnreachable) {
    transport_.push_failure(NetworkError::kHttpsConnectFailed);
    transport_.push_failure(NetworkError::kResolveFailed);
    {
        HttpsRendezvousClient client(transport_, listener_);
        client.register_invite(registration_with_token(0x01));
        client.resolve_invite(code_of("ABCDE12345"));
    }
    const auto registered = listener_.registered();
    ASSERT_EQ(registered.size(), 1U);
    EXPECT_EQ(registered[0].result.error(), ErrorCode::kUnreachable);
    const auto resolved = listener_.resolved();
    ASSERT_EQ(resolved.size(), 1U);
    EXPECT_EQ(resolved[0].result.error(), ErrorCode::kUnreachable);
}

TEST_F(HttpsRendezvousClientTest, PassesTheResolvedInviteWithTheRequestedCode) {
    transport_.push_response(200, json{{"code", "ABCDE12345"},
                                       {"host_public_key", kAa32},
                                       {"invite_token", kAa32},
                                       {"rendezvous_id", kEleven16}}
                                      .dump());
    {
        HttpsRendezvousClient client(transport_, listener_);
        client.resolve_invite(code_of("abcde-12345"));
    }
    const auto requests = transport_.requests();
    ASSERT_EQ(requests.size(), 1U);
    EXPECT_EQ(requests[0].path, "/v1/invites/resolve");
    EXPECT_EQ(json::parse(requests[0].body), (json{{"code", "ABCDE12345"}}));
    const auto resolved = listener_.resolved();
    ASSERT_EQ(resolved.size(), 1U);
    EXPECT_EQ(resolved[0].code, code_of("ABCDE12345"));
    ASSERT_TRUE(resolved[0].result.has_value());
}

TEST_F(HttpsRendezvousClientTest, PassesTheServerErrorCodeOfAResolution) {
    transport_.push_response(410, json{{"error", "INVITE_REVOKED"}}.dump());
    {
        HttpsRendezvousClient client(transport_, listener_);
        client.resolve_invite(code_of("ABCDE12345"));
    }
    const auto resolved = listener_.resolved();
    ASSERT_EQ(resolved.size(), 1U);
    EXPECT_EQ(resolved[0].result.error(), ErrorCode::kInviteRevoked);
}

TEST_F(HttpsRendezvousClientTest, SendsRequestsInCallOrderAndFlushesThemOnDestruction) {
    // 発行し直しでは、古い招待の失効と新しい招待の登録が続く。サーバーに届く順序を保つ。
    transport_.push_response(204, "");
    transport_.push_response(201, json{{"code", "ZZZZZ99999"}, {"revoke_key", kAa32}}.dump());
    {
        HttpsRendezvousClient client(transport_, listener_);
        session::RevokeKey key{};
        key.fill(0xAA);
        client.revoke_invite(code_of("ABCDE12345"), key);
        client.register_invite(registration_with_token(0x02));
    }
    const auto requests = transport_.requests();
    ASSERT_EQ(requests.size(), 2U);
    EXPECT_EQ(requests[0].path, "/v1/invites/revoke");
    EXPECT_EQ(json::parse(requests[0].body), (json{{"code", "ABCDE12345"}, {"revoke_key", kAa32}}));
    EXPECT_EQ(requests[1].path, "/v1/invites");
    // 失効には結果を返さず、登録の結果だけが届く。
    EXPECT_EQ(listener_.registered().size(), 1U);
    EXPECT_TRUE(listener_.resolved().empty());
}

}  // namespace
}  // namespace vocroz::network
