// SPDX-License-Identifier: AGPL-3.0-only
//
// quote_argument() のテスト。CommandLineToArgvW の規則で元の引数に戻る形になることを、
// 規則の代表的な場合（空白、末尾の \、引用符、空の引数）で確かめる。

#include "command_line.h"

#include <gtest/gtest.h>

namespace vocroz::pa_desktop {
namespace {

TEST(CommandLineTest, LeavesSimpleArgumentsAlone) {
    EXPECT_EQ(quote_argument(L"--log-level=info"), L"--log-level=info");
    EXPECT_EQ(quote_argument(L"--data-dir=C:\\Users\\a\\VOCROZ"),
              L"--data-dir=C:\\Users\\a\\VOCROZ");
}

TEST(CommandLineTest, QuotesSpaces) {
    EXPECT_EQ(quote_argument(L"--data-dir=C:\\Users\\Ana Lee\\VOCROZ"),
              L"\"--data-dir=C:\\Users\\Ana Lee\\VOCROZ\"");
}

TEST(CommandLineTest, DoublesBackslashesBeforeTheClosingQuote) {
    // 末尾の \ をそのままにすると、閉じの引用符がエスケープされて引数が続いてしまう。
    EXPECT_EQ(quote_argument(L"--data-dir=D:\\my data\\"), L"\"--data-dir=D:\\my data\\\\\"");
}

TEST(CommandLineTest, EscapesQuotes) {
    EXPECT_EQ(quote_argument(L"a \"b\""), L"\"a \\\"b\\\"\"");
    EXPECT_EQ(quote_argument(L"a\\\"b"), L"\"a\\\\\\\"b\"");
}

TEST(CommandLineTest, QuotesEmptyArgument) {
    EXPECT_EQ(quote_argument(L""), L"\"\"");
}

}  // namespace
}  // namespace vocroz::pa_desktop
