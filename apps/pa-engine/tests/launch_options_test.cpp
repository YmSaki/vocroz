// SPDX-License-Identifier: AGPL-3.0-only
//
// parse_launch_options() の単体テスト。

#include "launch_options.h"

#include <array>
#include <variant>

#include <gtest/gtest.h>

namespace vocroz::engine {
namespace {

template <std::size_t N>
std::variant<LaunchOptions, LaunchError> parse(const std::array<const char*, N>& args) {
    return parse_launch_options(common::Args{args.data(), args.size()});
}

TEST(LaunchOptionsTest, RunsWithDefaultsWithoutArguments) {
    const auto parsed = parse(std::array<const char*, 0>{});
    ASSERT_TRUE(std::holds_alternative<LaunchOptions>(parsed));
    const auto& options = std::get<LaunchOptions>(parsed);
    EXPECT_EQ(options.command, Command::kRun);
    EXPECT_FALSE(options.data_dir.has_value());
    EXPECT_EQ(options.log_level, common::LogLevel::kInfo);
}

TEST(LaunchOptionsTest, ReadsDataDirAndLogLevel) {
    const auto parsed = parse(std::array{"--data-dir=C:\\vocroz test", "--log-level=debug"});
    ASSERT_TRUE(std::holds_alternative<LaunchOptions>(parsed));
    const auto& options = std::get<LaunchOptions>(parsed);
    EXPECT_EQ(options.data_dir, "C:\\vocroz test");
    EXPECT_EQ(options.log_level, common::LogLevel::kDebug);
}

TEST(LaunchOptionsTest, AcceptsVersion) {
    const auto parsed = parse(std::array{"--version"});
    ASSERT_TRUE(std::holds_alternative<LaunchOptions>(parsed));
    EXPECT_EQ(std::get<LaunchOptions>(parsed).command, Command::kVersion);
}

// remote-pa の引数（--mode、--join など）はエンジンでは受け付けない。黙って無視すると、
// 起動側の誤りに気付けないため。
TEST(LaunchOptionsTest, RejectsUnknownArguments) {
    EXPECT_TRUE(std::holds_alternative<LaunchError>(parse(std::array{"--mode=host"})));
    EXPECT_TRUE(std::holds_alternative<LaunchError>(parse(std::array{"--data-dir"})));
}

TEST(LaunchOptionsTest, RejectsEmptyOrRepeatedValues) {
    EXPECT_TRUE(std::holds_alternative<LaunchError>(parse(std::array{"--data-dir="})));
    EXPECT_TRUE(
        std::holds_alternative<LaunchError>(parse(std::array{"--data-dir=a", "--data-dir=b"})));
    EXPECT_TRUE(std::holds_alternative<LaunchError>(
        parse(std::array{"--log-level=info", "--log-level=warn"})));
}

TEST(LaunchOptionsTest, RejectsUnknownLogLevel) {
    EXPECT_TRUE(std::holds_alternative<LaunchError>(parse(std::array{"--log-level=verbose"})));
}

}  // namespace
}  // namespace vocroz::engine
