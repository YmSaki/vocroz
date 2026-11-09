// SPDX-License-Identifier: AGPL-3.0-only
//
// DpapiDataProtector のテスト（Windows だけ）。実際の DPAPI で往復できることと、
// 用途の違いや改ざんで復号に失敗することを確かめる。

#include "win/dpapi_data_protector.h"

#include <array>

#include <gtest/gtest.h>

namespace vocroz::persistence {
namespace {

constexpr std::array<std::uint8_t, 32> kKey{1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11,
                                            12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
                                            23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

TEST(DpapiDataProtectorTest, RoundTripsWithSamePurpose) {
    DpapiDataProtector protector;
    const auto sealed = protector.protect(kKey, "host-static-key");
    ASSERT_TRUE(sealed);
    const auto plain = protector.unprotect(*sealed, "host-static-key");
    ASSERT_TRUE(plain);
    EXPECT_EQ(*plain, std::vector<std::uint8_t>(kKey.begin(), kKey.end()));
}

TEST(DpapiDataProtectorTest, FailsWithDifferentPurpose) {
    DpapiDataProtector protector;
    const auto sealed = protector.protect(kKey, "host-static-key");
    ASSERT_TRUE(sealed);
    EXPECT_FALSE(protector.unprotect(*sealed, "client-static-key"));
}

TEST(DpapiDataProtectorTest, FailsWhenTampered) {
    DpapiDataProtector protector;
    auto sealed = protector.protect(kKey, "host-static-key");
    ASSERT_TRUE(sealed);
    sealed->back() = static_cast<char>(sealed->back() ^ 0x01);
    EXPECT_FALSE(protector.unprotect(*sealed, "host-static-key"));
}

}  // namespace
}  // namespace vocroz::persistence
