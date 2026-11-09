// SPDX-License-Identifier: AGPL-3.0-only
//
// UnavailableRendezvousClient のテスト。招待の解決が UNREACHABLE として受け箱に入り、
// コントロールスレッドで取り出せることを確かめる。

#include "unavailable_rendezvous_client.h"

#include <gtest/gtest.h>

#include "invite_inbox.h"

namespace vocroz::client {
namespace {

TEST(UnavailableRendezvousClientTest, ResolvesEveryInviteAsUnreachable) {
    InviteInbox inbox;
    UnavailableRendezvousClient client(inbox);
    const auto code = session::parse_invite_code("ABCDE12345").value();
    client.resolve_invite(code);

    int calls = 0;
    inbox.drain([&](const session::InviteCode& c, const InviteInbox::ResolveResult& r) {
        ++calls;
        EXPECT_EQ(c, code);
        ASSERT_FALSE(r);
        EXPECT_EQ(r.error(), control_protocol::ErrorCode::kUnreachable);
    });
    EXPECT_EQ(calls, 1);
}

}  // namespace
}  // namespace vocroz::client
