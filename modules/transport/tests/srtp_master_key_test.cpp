// SPDX-License-Identifier: AGPL-3.0-only
//
// SRTP のマスターキーのパース（信頼境界）、書式化、生成のテスト。

#include "vocroz/transport/srtp_master_key.h"

#include <algorithm>
#include <string>

#include <gtest/gtest.h>

namespace vocroz::transport {
namespace {

// RFC 4568 の例に出てくる鍵（30 バイト）。
constexpr const char* kRfcExampleKey = "WVNfX19zZW1jdGwgKCkgewkyMjA7fQp9CnVubGVz";

TEST(SrtpMasterKeyTest, ParsesKeyWithInlinePrefixAndSurroundingWhitespace) {
    const auto with_prefix =
        parse_srtp_master_key(std::string("  inline:") + kRfcExampleKey + "\n");
    const auto without_prefix = parse_srtp_master_key(kRfcExampleKey);
    ASSERT_TRUE(with_prefix);
    ASSERT_TRUE(without_prefix);
    EXPECT_TRUE(
        std::ranges::equal(with_prefix.value().material(), without_prefix.value().material()));
    EXPECT_EQ(with_prefix.value().material()[0], 'Y');  // Base64 の "WVNf" は "YS_" の先頭
}

TEST(SrtpMasterKeyTest, FormatsAsInlineAttributeThatParsesBackToTheSameKey) {
    const auto key = parse_srtp_master_key(kRfcExampleKey);
    ASSERT_TRUE(key);
    const std::string text = format_srtp_master_key(key.value());
    EXPECT_EQ(text, std::string("inline:") + kRfcExampleKey);
    const auto reparsed = parse_srtp_master_key(text);
    ASSERT_TRUE(reparsed);
    EXPECT_TRUE(std::ranges::equal(key.value().material(), reparsed.value().material()));
}

TEST(SrtpMasterKeyTest, RejectsLifetimeAndMkiParameters) {
    const auto with_lifetime = parse_srtp_master_key(std::string(kRfcExampleKey) + "|2^20");
    ASSERT_FALSE(with_lifetime);
    EXPECT_EQ(with_lifetime.error(), SrtpKeyError::kUnsupportedParameter);
    const auto with_mki = parse_srtp_master_key(std::string(kRfcExampleKey) + "|1:4");
    ASSERT_FALSE(with_mki);
    EXPECT_EQ(with_mki.error(), SrtpKeyError::kUnsupportedParameter);
}

TEST(SrtpMasterKeyTest, RejectsWrongLengthAndNonBase64Text) {
    for (const std::string& text :
         {std::string{}, std::string("inline:"), std::string(kRfcExampleKey).substr(0, 39),
          std::string(kRfcExampleKey) + "AAAA", std::string(40, '*')}) {
        const auto key = parse_srtp_master_key(text);
        ASSERT_FALSE(key) << text;
        EXPECT_EQ(key.error(), SrtpKeyError::kMalformed) << text;
    }
}

TEST(SrtpMasterKeyTest, RejectsAllZeroKeySoAnUnsetFieldIsNeverUsed) {
    const auto key = parse_srtp_master_key(std::string(40, 'A'));
    ASSERT_FALSE(key);
    EXPECT_EQ(key.error(), SrtpKeyError::kAllZero);
}

TEST(SrtpMasterKeyTest, GeneratesDistinctKeysThatRoundTripThroughText) {
    const auto first = generate_srtp_master_key();
    const auto second = generate_srtp_master_key();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_FALSE(std::ranges::equal(first.value().material(), second.value().material()));
    const auto reparsed = parse_srtp_master_key(format_srtp_master_key(first.value()));
    ASSERT_TRUE(reparsed);
    EXPECT_TRUE(std::ranges::equal(first.value().material(), reparsed.value().material()));
}

TEST(SrtpMasterKeyTest, MovedFromKeyIsWiped) {
    auto key = parse_srtp_master_key(kRfcExampleKey);
    ASSERT_TRUE(key);
    SrtpMasterKey moved = std::move(key.value());
    EXPECT_TRUE(std::ranges::all_of(key.value().material(), [](std::uint8_t b) { return b == 0; }));
    EXPECT_EQ(moved.material()[0], 'Y');
}

}  // namespace
}  // namespace vocroz::transport
