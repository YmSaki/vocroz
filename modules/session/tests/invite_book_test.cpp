// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の招待の記録のテスト。

#include "invite_book.h"

#include <chrono>

#include <gtest/gtest.h>

namespace vocroz::session {
namespace {

using control_protocol::ErrorCode;
using namespace std::chrono_literals;

InviteToken token(std::uint8_t value) {
    InviteToken t{};
    t.fill(value);
    return t;
}

RegisteredInvite registered(std::string_view code) {
    return RegisteredInvite{parse_invite_code(code).value(), RevokeKey{}};
}

const InviteBook::TimePoint kStart{};

TEST(InviteBookTest, AcceptsTheCurrentInviteUntilItExpires) {
    InviteBook book;
    book.begin_issue(token(1));
    // 登録が終わるまでは、そのトークンで参加できない。
    EXPECT_EQ(book.verify(token(1), kStart).error(), ErrorCode::kInviteInvalid);
    book.complete_issue(token(1), registered("AAAAA11111"), kStart + 30min);
    EXPECT_TRUE(book.verify(token(1), kStart + 29min));
    EXPECT_EQ(book.verify(token(1), kStart + 30min).error(), ErrorCode::kInviteExpired);
}

TEST(InviteBookTest, DistinguishesExpiredRevokedAndUnknownInvites) {
    InviteBook book;
    book.begin_issue(token(1));
    book.complete_issue(token(1), registered("AAAAA11111"), kStart + 30min);
    EXPECT_TRUE(book.expire_if_due(kStart + 30min));
    book.begin_issue(token(2));
    book.complete_issue(token(2), registered("BBBBB22222"), kStart + 60min);
    const auto revoked = book.revoke_current();
    ASSERT_TRUE(revoked);
    EXPECT_EQ(revoked->code.normalized(), "BBBBB22222");
    EXPECT_EQ(book.current(), nullptr);

    EXPECT_EQ(book.verify(token(1), kStart + 31min).error(), ErrorCode::kInviteExpired);
    EXPECT_EQ(book.verify(token(2), kStart + 31min).error(), ErrorCode::kInviteRevoked);
    EXPECT_EQ(book.verify(token(3), kStart + 31min).error(), ErrorCode::kInviteInvalid);
}

TEST(InviteBookTest, ReplacingTheInviteRevokesThePreviousOne) {
    InviteBook book;
    book.begin_issue(token(1));
    book.complete_issue(token(1), registered("AAAAA11111"), kStart + 30min);
    book.begin_issue(token(2));
    book.complete_issue(token(2), registered("BBBBB22222"), kStart + 30min);
    EXPECT_EQ(book.verify(token(1), kStart).error(), ErrorCode::kInviteRevoked);
    EXPECT_TRUE(book.verify(token(2), kStart));
}

TEST(InviteBookTest, ClearForgetsEveryInvite) {
    InviteBook book;
    book.begin_issue(token(1));
    book.complete_issue(token(1), registered("AAAAA11111"), kStart + 30min);
    book.clear();
    EXPECT_FALSE(book.issuing());
    EXPECT_EQ(book.verify(token(1), kStart).error(), ErrorCode::kInviteInvalid);
}

}  // namespace
}  // namespace vocroz::session
