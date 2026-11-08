// SPDX-License-Identifier: AGPL-3.0-only
//
// 招待コード、招待 URL、vocroz:// の引数のパースのテスト。

#include "vocroz/session/invite_code.h"

#include <string>

#include <gtest/gtest.h>

namespace vocroz::session {
namespace {

std::string normalized(std::string_view text) {
    const auto code = parse_invite(text);
    return code ? code.value().normalized() : std::string{"<invalid>"};
}

TEST(InviteCodeTest, NormalizesCaseHyphensAndConfusableLetters) {
    // ランデブーサーバー（infra/rendezvous/internal/invite/code.go）と同じ規則で吸収する。
    EXPECT_EQ(normalized("abcde-12345"), "ABCDE12345");
    EXPECT_EQ(normalized("ABCDE 12345"), "ABCDE12345");
    EXPECT_EQ(normalized("OIL00-abcde"), "01100ABCDE");
}

TEST(InviteCodeTest, DisplaysTwoGroupsOfFive) {
    const auto code = parse_invite_code("abcde12345");
    ASSERT_TRUE(code);
    EXPECT_EQ(code.value().display(), "ABCDE-12345");
}

TEST(InviteCodeTest, RejectsWrongLengthAndLettersOutsideCrockfordBase32) {
    EXPECT_FALSE(parse_invite("ABCDE-1234"));
    EXPECT_FALSE(parse_invite("ABCDE-123456"));
    EXPECT_FALSE(parse_invite("ABCDE-1234U"));
    EXPECT_FALSE(parse_invite(""));
    EXPECT_FALSE(parse_invite("ＡBCDE-12345"));  // 全角文字
}

TEST(InviteCodeTest, ReadsTheCodeFromTheFragmentOfAnInviteUrl) {
    EXPECT_EQ(normalized("https://vocroz.example/join#ABCDE-12345"), "ABCDE12345");
    EXPECT_EQ(normalized("  HTTPS://vocroz.example/join/#abcde-12345\n"), "ABCDE12345");
    EXPECT_EQ(normalized("https://vocroz.example/join#ABCDE%2D12345"), "ABCDE12345");
}

TEST(InviteCodeTest, RejectsInviteUrlsWithOtherPathsOrSchemes) {
    EXPECT_FALSE(parse_invite("https://vocroz.example/other#ABCDE-12345"));
    EXPECT_FALSE(parse_invite("https://vocroz.example/join?code=ABCDE-12345"));
    EXPECT_FALSE(parse_invite("http://vocroz.example/join#ABCDE-12345"));
    EXPECT_FALSE(parse_invite("https:///join#ABCDE-12345"));
    EXPECT_FALSE(parse_invite("https://vocroz.example/join#ABCDE%2"));
}

TEST(InviteCodeTest, AcceptsOnlyTheJoinActionOfTheUrlScheme) {
    EXPECT_EQ(normalized("vocroz://join?code=ABCDE-12345"), "ABCDE12345");
    EXPECT_EQ(normalized("vocroz://join/?lang=ja&code=abcde-12345"), "ABCDE12345");
    EXPECT_FALSE(parse_invite("vocroz://update?code=ABCDE-12345"));
    EXPECT_FALSE(parse_invite("vocroz://join"));
    EXPECT_FALSE(parse_invite("vocroz://join?token=ABCDE-12345"));
}

TEST(InviteCodeTest, RejectsOverlongInput) {
    EXPECT_FALSE(parse_invite(std::string(4096, 'A')));
}

TEST(InviteCodeTest, BuildsTheInviteUrlWithTheDisplayCodeInTheFragment) {
    const auto code = parse_invite_code("ABCDE12345");
    ASSERT_TRUE(code);
    EXPECT_EQ(make_invite_url("https://vocroz.example/", code.value()),
              "https://vocroz.example/join#ABCDE-12345");
    // 組み立てた URL はそのまま読み戻せる。
    EXPECT_EQ(normalized(make_invite_url("https://vocroz.example", code.value())), "ABCDE12345");
}

}  // namespace
}  // namespace vocroz::session
