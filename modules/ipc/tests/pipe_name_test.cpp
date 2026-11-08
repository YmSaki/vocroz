// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-engine のパイプの名前の導出の単体テスト。

#include "vocroz/ipc/pipe_name.h"

#include <string>

#include <gtest/gtest.h>

namespace vocroz::ipc {
namespace {

constexpr std::string_view kSid = "S-1-5-21-1111111111-2222222222-3333333333-1001";

TEST(PipeNameTest, StartsWithPrefixAndSid) {
    const std::string name = make_engine_pipe_name(kSid, R"(C:\Users\a\AppData\Local\VOCROZ)");
    const std::string prefix = std::string{R"(\\.\pipe\vocroz-engine-)"} + std::string{kSid} + "-";
    ASSERT_EQ(name.substr(0, prefix.size()), prefix);
    // ハッシュは 16 桁の小文字の 16 進数。
    const std::string hash = name.substr(prefix.size());
    ASSERT_EQ(hash.size(), 16U);
    EXPECT_EQ(hash.find_first_not_of("0123456789abcdef"), std::string::npos);
}

TEST(PipeNameTest, HashMatchesFnv1aOfNormalizedPath) {
    // FNV-1a（64 ビット）の空文字列の値は、オフセット基底そのもの。
    EXPECT_EQ(make_engine_pipe_name("S", ""), R"(\\.\pipe\vocroz-engine-S-cbf29ce484222325)");
    // "a" の FNV-1a（64 ビット）は af63dc4c8601ec8c（公開されている参照値）。
    EXPECT_EQ(make_engine_pipe_name("S", "A"), R"(\\.\pipe\vocroz-engine-S-af63dc4c8601ec8c)");
}

TEST(PipeNameTest, IgnoresCaseSeparatorStyleAndTrailingSeparator) {
    const std::string base = make_engine_pipe_name(kSid, R"(C:\Users\a\VOCROZ)");
    EXPECT_EQ(make_engine_pipe_name(kSid, R"(c:\users\A\vocroz)"), base);
    EXPECT_EQ(make_engine_pipe_name(kSid, "C:/Users/a/VOCROZ"), base);
    EXPECT_EQ(make_engine_pipe_name(kSid, R"(C:\Users\a\VOCROZ\)"), base);
}

TEST(PipeNameTest, DiffersByDataDirAndUser) {
    const std::string base = make_engine_pipe_name(kSid, R"(C:\data\host)");
    EXPECT_NE(make_engine_pipe_name(kSid, R"(C:\data\host2)"), base);
    EXPECT_NE(make_engine_pipe_name("S-1-5-21-9", R"(C:\data\host)"), base);
}

TEST(PipeNameTest, KeepsNonAsciiPathBytes) {
    // ASCII 以外は小文字化しない（大文字と小文字の対応はロケールに依存するため）。
    EXPECT_NE(make_engine_pipe_name(kSid, "C:\\データ"), make_engine_pipe_name(kSid, "C:\\data"));
}

}  // namespace
}  // namespace vocroz::ipc
