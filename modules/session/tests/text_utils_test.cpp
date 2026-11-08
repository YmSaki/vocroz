// SPDX-License-Identifier: AGPL-3.0-only
//
// 外部から来る文字列の検査のテスト。

#include "detail/text_utils.h"

#include <string>

#include <gtest/gtest.h>

namespace vocroz::session::detail {
namespace {

TEST(TextUtilsTest, AcceptsJapaneseAndEmojiWithinTheLimit) {
    EXPECT_TRUE(is_displayable_text("Vo 佐藤", 64));
    EXPECT_TRUE(is_displayable_text("🎺", 4));
    EXPECT_TRUE(is_displayable_text("", 0));
}

TEST(TextUtilsTest, RejectsTextLongerThanTheLimitInBytes) {
    EXPECT_FALSE(is_displayable_text("佐藤", 5));  // 6 バイト
}

TEST(TextUtilsTest, RejectsControlCharacters) {
    EXPECT_FALSE(is_displayable_text("a\nb", 64));
    EXPECT_FALSE(is_displayable_text(std::string{"a\0b", 3}, 64));
    EXPECT_FALSE(is_displayable_text("a\x7f", 64));
    EXPECT_FALSE(is_displayable_text("a\xc2\x85", 64));  // U+0085
}

TEST(TextUtilsTest, RejectsMalformedUtf8) {
    EXPECT_FALSE(is_displayable_text("\xff", 64));
    EXPECT_FALSE(is_displayable_text("\xe3\x81", 64));          // 途中で切れている
    EXPECT_FALSE(is_displayable_text("\xc0\xaf", 64));          // 過長な表現
    EXPECT_FALSE(is_displayable_text("\xed\xa0\x80", 64));      // サロゲート
    EXPECT_FALSE(is_displayable_text("\xf4\x90\x80\x80", 64));  // U+10FFFF を超える
}

}  // namespace
}  // namespace vocroz::session::detail
