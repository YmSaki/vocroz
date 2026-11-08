// SPDX-License-Identifier: AGPL-3.0-only
//
// vocroz::common::Result の単体テスト。

#include "vocroz/common/result.h"

#include <string>

#include <gtest/gtest.h>

namespace vocroz::common {
namespace {

enum class TestError {
    kInvalid,
    kTimeout,
};

Result<int, TestError> parse_positive(int input) {
    if (input <= 0) {
        return make_unexpected(TestError::kInvalid);
    }
    return input;
}

Result<void, TestError> check(bool ok) {
    if (!ok) {
        return make_unexpected(TestError::kTimeout);
    }
    return {};
}

TEST(ResultTest, HoldsValueOnSuccess) {
    const auto result = parse_positive(3);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(result.value(), 3);
}

TEST(ResultTest, HoldsErrorOnFailure) {
    const auto result = parse_positive(0);
    ASSERT_FALSE(result.has_value());
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_EQ(result.error(), TestError::kInvalid);
}

TEST(ResultTest, ValueOrReturnsFallbackOnlyOnFailure) {
    EXPECT_EQ(parse_positive(5).value_or(-1), 5);
    EXPECT_EQ(parse_positive(-5).value_or(-1), -1);
}

TEST(ResultTest, DistinguishesValueAndErrorOfSameType) {
    // 値とエラーが同じ型でも、どちらで作ったかが保たれる。
    const Result<int, int> ok = 7;
    const Result<int, int> failed = make_unexpected(7);
    EXPECT_TRUE(ok.has_value());
    EXPECT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error(), 7);
}

TEST(ResultTest, MovesValueOut) {
    Result<std::string, TestError> result = std::string{"vocroz"};
    const std::string moved = std::move(result).value();
    EXPECT_EQ(moved, "vocroz");
}

TEST(ResultTest, VoidResultReportsSuccessAndError) {
    EXPECT_TRUE(check(true).has_value());
    const auto failed = check(false);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error(), TestError::kTimeout);
}

}  // namespace
}  // namespace vocroz::common
