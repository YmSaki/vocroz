// SPDX-License-Identifier: AGPL-3.0-only
//
// base64url（パディングなし）の符号化と復号のテスト。期待値は Go の base64.RawURLEncoding と
// Python の base64.urlsafe_b64encode（パディングを除いたもの）で作った。

#include "wire/base64url.h"

#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::network::wire {
namespace {

std::vector<std::uint8_t> bytes_of(std::string_view text) {
    return {text.begin(), text.end()};
}

TEST(Base64UrlTest, EncodesRfc4648VectorsWithoutPadding) {
    EXPECT_EQ(encode_base64url(bytes_of("")), "");
    EXPECT_EQ(encode_base64url(bytes_of("f")), "Zg");
    EXPECT_EQ(encode_base64url(bytes_of("fo")), "Zm8");
    EXPECT_EQ(encode_base64url(bytes_of("foo")), "Zm9v");
    EXPECT_EQ(encode_base64url(bytes_of("foob")), "Zm9vYg");
    EXPECT_EQ(encode_base64url(bytes_of("fooba")), "Zm9vYmE");
    EXPECT_EQ(encode_base64url(bytes_of("foobar")), "Zm9vYmFy");
}

TEST(Base64UrlTest, UsesTheUrlSafeAlphabet) {
    const std::vector<std::uint8_t> bytes = {0xFB, 0xFF};
    EXPECT_EQ(encode_base64url(bytes), "-_8");
}

TEST(Base64UrlTest, RoundTripsAThirtyTwoByteKey) {
    std::vector<std::uint8_t> key(32);
    std::iota(key.begin(), key.end(), std::uint8_t{0});
    const std::string text = encode_base64url(key);
    EXPECT_EQ(text, "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8");
    const auto decoded = decode_base64url(text);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, key);
}

TEST(Base64UrlTest, DecodesEveryRemainderLength) {
    EXPECT_EQ(decode_base64url(""), std::vector<std::uint8_t>{});
    EXPECT_EQ(decode_base64url("Zg"), bytes_of("f"));
    EXPECT_EQ(decode_base64url("Zm8"), bytes_of("fo"));
    EXPECT_EQ(decode_base64url("Zm9v"), bytes_of("foo"));
}

TEST(Base64UrlTest, RejectsPaddingAndTheStandardAlphabet) {
    EXPECT_FALSE(decode_base64url("Zg==").has_value());
    EXPECT_FALSE(decode_base64url("+/8").has_value());
    EXPECT_FALSE(decode_base64url("Zm 9v").has_value());
}

TEST(Base64UrlTest, RejectsALengthThatNoByteStringEncodesTo) {
    EXPECT_FALSE(decode_base64url("Zm9vY").has_value());
}

TEST(Base64UrlTest, RejectsNonZeroTrailingBits) {
    // "Zg" が "f" の正しい表記で、"Zh" は余りのビットが 0 でない別の表記。
    EXPECT_FALSE(decode_base64url("Zh").has_value());
}

}  // namespace
}  // namespace vocroz::network::wire
