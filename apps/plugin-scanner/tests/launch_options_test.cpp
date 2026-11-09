// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-plugin-scanner の起動オプションのパースの単体テスト。

#include "launch_options.h"

#include <array>
#include <variant>

#include <gtest/gtest.h>

namespace vocroz::plugin_scanner {
namespace {

template <std::size_t N>
std::variant<LaunchOptions, LaunchError> parse(const std::array<const char*, N>& args) {
    return parse_launch_options(common::Args{args.data(), args.size()});
}

TEST(LaunchOptionsTest, ParsesVersion) {
    const auto result = parse(std::array{"--version"});
    ASSERT_TRUE(std::holds_alternative<LaunchOptions>(result));
    EXPECT_EQ(std::get<LaunchOptions>(result).command, Command::kVersion);
}

TEST(LaunchOptionsTest, ParsesScanWithFile) {
    const auto result = parse(std::array{"--scan", "C:/VST3/リバーブ.vst3"});
    ASSERT_TRUE(std::holds_alternative<LaunchOptions>(result));
    EXPECT_EQ(std::get<LaunchOptions>(result).command, Command::kScan);
    EXPECT_EQ(std::get<LaunchOptions>(result).plugin_file, "C:/VST3/リバーブ.vst3");
}

TEST(LaunchOptionsTest, RejectsMissingExtraOrUnknownArguments) {
    EXPECT_TRUE(std::holds_alternative<LaunchError>(parse(std::array<const char*, 0>{})));
    EXPECT_TRUE(std::holds_alternative<LaunchError>(parse(std::array{"--scan"})));
    EXPECT_TRUE(std::holds_alternative<LaunchError>(parse(std::array{"--scan", ""})));
    EXPECT_TRUE(std::holds_alternative<LaunchError>(parse(std::array{"--scan", "a", "b"})));
    EXPECT_TRUE(std::holds_alternative<LaunchError>(parse(std::array{"--version", "--scan"})));
    EXPECT_TRUE(std::holds_alternative<LaunchError>(parse(std::array{"--list"})));
}

}  // namespace
}  // namespace vocroz::plugin_scanner
