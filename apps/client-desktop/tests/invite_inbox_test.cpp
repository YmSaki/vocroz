// SPDX-License-Identifier: AGPL-3.0-only
//
// 招待の解決の結果の受け箱（InviteInbox）のテスト。成功と失敗の結果が、招待コードとともに
// そのまま取り出せることを確かめる。

#include "invite_inbox.h"

#include <vector>

#include <gtest/gtest.h>

namespace vocroz::client {
namespace {

session::InviteCode code(std::string_view text) {
    return session::parse_invite_code(text).value();
}

TEST(InviteInboxTest, CarriesResolvedInvite) {
    InviteInbox inbox;
    session::ResolvedInvite resolved;
    resolved.endpoint.host_public_key[0] = 7;
    resolved.endpoint.rendezvous_id[1] = 8;
    resolved.invite_token[2] = 9;
    ASSERT_TRUE(inbox.push(code("ABCDE-12345"), resolved));

    std::vector<std::pair<session::InviteCode, InviteInbox::ResolveResult>> received;
    inbox.drain([&](const session::InviteCode& c, const InviteInbox::ResolveResult& r) {
        received.emplace_back(c, r);
    });
    ASSERT_EQ(received.size(), 1U);
    EXPECT_EQ(received[0].first, code("ABCDE12345"));
    ASSERT_TRUE(received[0].second);
    EXPECT_EQ(received[0].second.value(), resolved);
}

TEST(InviteInboxTest, CarriesFailure) {
    InviteInbox inbox;
    ASSERT_TRUE(inbox.push(code("ABCDE12345"),
                           common::make_unexpected(control_protocol::ErrorCode::kInviteExpired)));

    int calls = 0;
    inbox.drain([&](const session::InviteCode&, const InviteInbox::ResolveResult& r) {
        ++calls;
        ASSERT_FALSE(r);
        EXPECT_EQ(r.error(), control_protocol::ErrorCode::kInviteExpired);
    });
    EXPECT_EQ(calls, 1);
}

TEST(InviteInboxTest, AcceptsResolutionsAsInviteResultListener) {
    // HttpsRendezvousClient には受け箱をリスナーとして渡す。解決の結果は push() と同じく入る。
    InviteInbox inbox;
    network::InviteResultListener& listener = inbox;
    listener.on_invite_resolved(code("ABCDE12345"),
                                common::make_unexpected(control_protocol::ErrorCode::kRejected));

    int calls = 0;
    inbox.drain([&](const session::InviteCode&, const InviteInbox::ResolveResult& r) {
        ++calls;
        ASSERT_FALSE(r);
        EXPECT_EQ(r.error(), control_protocol::ErrorCode::kRejected);
    });
    EXPECT_EQ(calls, 1);
}

}  // namespace
}  // namespace vocroz::client
