// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-updater の起動オプションのパースの単体テスト。

#include "launch_options.h"

#include <vector>

#include <gtest/gtest.h>

namespace vocroz::updater {
namespace {

std::variant<LaunchOptions, LaunchError> parse(std::vector<const char*> args) {
    return parse_launch_options(common::Args{args.data(), args.size()});
}

LaunchOptions ok(std::vector<const char*> args) {
    const auto result = parse(std::move(args));
    EXPECT_TRUE(std::holds_alternative<LaunchOptions>(result));
    return std::holds_alternative<LaunchOptions>(result) ? std::get<LaunchOptions>(result)
                                                         : LaunchOptions{};
}

bool is_error(std::vector<const char*> args) {
    return std::holds_alternative<LaunchError>(parse(std::move(args)));
}

TEST(LaunchOptionsTest, ParsesEachCommand) {
    EXPECT_EQ(ok({"--version"}).command, Command::kVersion);
    EXPECT_EQ(ok({"--check"}).command, Command::kCheck);
    EXPECT_EQ(ok({"--apply"}).command, Command::kApply);
}

TEST(LaunchOptionsTest, ParsesDirectoriesAndRestartArguments) {
    const LaunchOptions options = ok({"--apply", "--restart", "--install-dir=C:\\VOCROZ",
                                      "--data-dir=D:\\data", "--", "--mode=client", "--"});
    EXPECT_TRUE(options.restart);
    EXPECT_EQ(options.install_dir, "C:\\VOCROZ");
    EXPECT_EQ(options.data_dir, "D:\\data");
    EXPECT_EQ(options.restart_args, (std::vector<std::string>{"--mode=client", "--"}));
}

TEST(LaunchOptionsTest, RequiresExactlyOneCommand) {
    EXPECT_TRUE(is_error({}));
    EXPECT_TRUE(is_error({"--check", "--apply"}));
    EXPECT_TRUE(is_error({"--apply", "--apply"}));
}

TEST(LaunchOptionsTest, RejectsUnknownAndContradictoryArguments) {
    EXPECT_TRUE(is_error({"--check", "--force"}));
    EXPECT_TRUE(is_error({"--check", "--restart"}));
    EXPECT_TRUE(is_error({"--apply", "--", "--mode=client"}));
    EXPECT_TRUE(is_error({"--apply", "--install-dir="}));
    EXPECT_TRUE(is_error({"--apply", "--data-dir=a", "--data-dir=b"}));
}

}  // namespace
}  // namespace vocroz::updater
