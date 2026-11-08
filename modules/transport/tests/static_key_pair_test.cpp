// SPDX-License-Identifier: AGPL-3.0-only
//
// StaticKeyPair の単体テスト。

#include "vocroz/transport/static_key_pair.h"

#include <algorithm>
#include <utility>

#include <gtest/gtest.h>

namespace vocroz::transport {
namespace {

bool is_all_zero(std::span<const std::uint8_t> bytes) {
    return std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t b) { return b == 0; });
}

TEST(StaticKeyPairTest, GeneratesDistinctNonZeroKeys) {
    auto first = StaticKeyPair::generate();
    auto second = StaticKeyPair::generate();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_FALSE(is_all_zero(first.value().public_key()));
    EXPECT_FALSE(is_all_zero(first.value().private_key()));
    EXPECT_NE(first.value().public_key(), second.value().public_key());
}

TEST(StaticKeyPairTest, MoveClearsPrivateKeyOfSource) {
    auto generated = StaticKeyPair::generate();
    ASSERT_TRUE(generated);
    StaticKeyPair source = std::move(generated).value();
    const PublicKey public_key = source.public_key();
    const StaticKeyPair destination = std::move(source);
    EXPECT_EQ(destination.public_key(), public_key);
    EXPECT_FALSE(is_all_zero(destination.private_key()));
    // ムーブ元に秘密鍵の複製を残さない。
    EXPECT_TRUE(is_all_zero(source.private_key()));  // NOLINT(bugprone-use-after-move)
}

}  // namespace
}  // namespace vocroz::transport
