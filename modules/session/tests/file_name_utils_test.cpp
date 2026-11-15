// SPDX-License-Identifier: AGPL-3.0-only
//
// 録音のファイル名の組み立てのテスト。

#include "detail/file_name_utils.h"

#include <gtest/gtest.h>

namespace vocroz::session::detail {
namespace {

constexpr std::size_t kMax = 80;

TEST(FileNameUtilsTest, KeepsOrdinaryNames) {
    EXPECT_EQ(to_file_name_part("Vocal", kMax, "Track"), "Vocal");
    EXPECT_EQ(to_file_name_part("ギター 1", kMax, "Track"), "ギター 1");
}

TEST(FileNameUtilsTest, ReplacesCharactersWindowsDoesNotAllow) {
    EXPECT_EQ(to_file_name_part("a<b>c:d\"e/f\\g|h?i*j", kMax, "Track"), "a_b_c_d_e_f_g_h_i_j");
    EXPECT_EQ(to_file_name_part("tab\there", kMax, "Track"), "tab_here");
}

TEST(FileNameUtilsTest, DoesNotProduceParentDirectoryNames) {
    // "/" は置き換え、末尾のピリオドは落とすので、".." や "../x" にはならない。
    EXPECT_EQ(to_file_name_part("..", kMax, "Track"), "Track");
    EXPECT_EQ(to_file_name_part("../x", kMax, "Track"), ".._x");
}

TEST(FileNameUtilsTest, TrimsTrailingSpacesAndDots) {
    EXPECT_EQ(to_file_name_part("Bass. . ", kMax, "Track"), "Bass");
    EXPECT_EQ(to_file_name_part("   ", kMax, "Track"), "Track");
}

TEST(FileNameUtilsTest, PrefixesReservedDeviceNames) {
    EXPECT_EQ(to_file_name_part("con", kMax, "Track"), "_con");
    EXPECT_EQ(to_file_name_part("COM3", kMax, "Track"), "_COM3");
    EXPECT_EQ(to_file_name_part("Nul.mix", kMax, "Track"), "_Nul.mix");
    EXPECT_EQ(to_file_name_part("COM0", kMax, "Track"), "COM0");
    EXPECT_EQ(to_file_name_part("Console", kMax, "Track"), "Console");
}

TEST(FileNameUtilsTest, TruncatesWithoutSplittingUtf8Characters) {
    // 「あ」は 3 バイト。4 バイトに切り詰めると 1 文字だけが残る。
    EXPECT_EQ(to_file_name_part("ああ", 4, "Track"), "あ");
    EXPECT_EQ(to_file_name_part("abcdef", 3, "Track"), "abc");
}

}  // namespace
}  // namespace vocroz::session::detail
