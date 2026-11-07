// SPDX-License-Identifier: AGPL-3.0-only
//
// 起動スタブのコマンドラインの切り出しの単体テスト。

#include "command_line.h"

#include <gtest/gtest.h>

namespace vocroz::launcher {
namespace {

TEST(ArgumentsOfTest, SkipsQuotedProgramNameWithSpaces) {
    EXPECT_EQ(arguments_of(L"\"C:\\Program Files\\VOCROZ\\vocroz.exe\" --mode=client"),
              L"--mode=client");
}

TEST(ArgumentsOfTest, SkipsUnquotedProgramName) {
    EXPECT_EQ(arguments_of(L"vocroz.exe  \t--mode=client --lang=ja"), L"--mode=client --lang=ja");
}

TEST(ArgumentsOfTest, KeepsQuotingOfArgumentsAsIs) {
    // URL スキームからの起動は "%1" で URL が引用符付きで渡る。
    EXPECT_EQ(arguments_of(L"\"vocroz.exe\" \"vocroz://join?code=AB\\\"CD\""),
              L"\"vocroz://join?code=AB\\\"CD\"");
}

TEST(ArgumentsOfTest, ReturnsEmptyWithoutArguments) {
    EXPECT_EQ(arguments_of(L"\"vocroz.exe\""), L"");
    EXPECT_EQ(arguments_of(L"vocroz.exe"), L"");
    EXPECT_EQ(arguments_of(L"vocroz.exe   "), L"");
    EXPECT_EQ(arguments_of(L""), L"");
}

}  // namespace
}  // namespace vocroz::launcher
