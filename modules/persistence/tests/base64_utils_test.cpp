// SPDX-License-Identifier: AGPL-3.0-only
//
// Base64 の符号化と復号のテスト。RFC 4648 の 10 節の例と、受け付けない書き方を確かめる。

#include "detail/base64_utils.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::persistence::detail {
namespace {

std::vector<std::uint8_t> bytes_of(std::string_view text) {
    return {text.begin(), text.end()};
}

TEST(Base64UtilsTest, EncodesTheRfcExamples) {
    EXPECT_EQ(encode_base64(bytes_of("")), "");
    EXPECT_EQ(encode_base64(bytes_of("f")), "Zg==");
    EXPECT_EQ(encode_base64(bytes_of("fo")), "Zm8=");
    EXPECT_EQ(encode_base64(bytes_of("foo")), "Zm9v");
    EXPECT_EQ(encode_base64(bytes_of("foob")), "Zm9vYg==");
    EXPECT_EQ(encode_base64(bytes_of("fooba")), "Zm9vYmE=");
    EXPECT_EQ(encode_base64(bytes_of("foobar")), "Zm9vYmFy");
}

TEST(Base64UtilsTest, DecodesWhatItEncodes) {
    std::vector<std::uint8_t> all;
    for (int i = 0; i < 256; ++i) {
        all.push_back(static_cast<std::uint8_t>(i));
    }
    for (std::size_t n = 0; n <= all.size(); n += 37) {
        const std::vector<std::uint8_t> part{all.begin(), all.begin() + static_cast<long>(n)};
        const auto decoded = decode_base64(encode_base64(part));
        ASSERT_TRUE(decoded);
        EXPECT_EQ(*decoded, part);
    }
}

TEST(Base64UtilsTest, RejectsMalformedText) {
    EXPECT_FALSE(decode_base64("Zg="));       // 長さが 4 の倍数でない
    EXPECT_FALSE(decode_base64("Zg==Zm8="));  // パディングが途中にある
    EXPECT_FALSE(decode_base64("Z==="));      // パディングが 3 文字
    EXPECT_FALSE(decode_base64("Zm9 "));      // 空白
    EXPECT_FALSE(decode_base64("Zm9-"));      // base64url の文字
    EXPECT_FALSE(decode_base64("Zh=="));      // 末尾の余りのビットが 0 でない
    EXPECT_FALSE(decode_base64("Zm9="));      // 末尾の余りのビットが 0 でない
}

}  // namespace
}  // namespace vocroz::persistence::detail
