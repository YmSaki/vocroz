// SPDX-License-Identifier: AGPL-3.0-only
//
// remote-pa の起動オプションのパースのテスト。モードの決まり方（引数なしは親機、招待は子機）と、
// 矛盾する引数、未知の引数、二重の指定をエラーにすることを確かめる。

#include "launch_options.h"

#include <initializer_list>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::pa_desktop {
namespace {

std::variant<LaunchOptions, LaunchError> parse(std::initializer_list<const char*> args) {
    const std::vector<const char*> argv(args);
    return parse_launch_options(common::Args(argv.data(), argv.size()));
}

LaunchOptions ok(std::initializer_list<const char*> args) {
    auto result = parse(args);
    EXPECT_TRUE(std::holds_alternative<LaunchOptions>(result))
        << std::get<LaunchError>(result).message;
    return std::get<LaunchOptions>(result);
}

bool fails(std::initializer_list<const char*> args) {
    return std::holds_alternative<LaunchError>(parse(args));
}

TEST(LaunchOptionsTest, NoArgumentsStartsHostMode) {
    const auto options = ok({});
    EXPECT_EQ(options.command, Command::kRun);
    EXPECT_EQ(options.mode, Mode::kHost);
    EXPECT_FALSE(options.join);
    EXPECT_TRUE(options.update_check);
    EXPECT_EQ(options.log_level, common::LogLevel::kInfo);
}

TEST(LaunchOptionsTest, ClientFlagAndModeSelectClientMode) {
    EXPECT_EQ(ok({"--client"}).mode, Mode::kClient);
    EXPECT_EQ(ok({"--mode=client"}).mode, Mode::kClient);
    EXPECT_EQ(ok({"--mode=host"}).mode, Mode::kHost);
}

TEST(LaunchOptionsTest, JoinImpliesClientMode) {
    const auto options = ok({"--join=ABCDE-12345"});
    EXPECT_EQ(options.mode, Mode::kClient);
    EXPECT_EQ(options.join, "ABCDE-12345");
}

TEST(LaunchOptionsTest, UrlSchemeArgumentIsTreatedAsJoin) {
    const auto options = ok({"vocroz://join?code=ABCDE-12345"});
    EXPECT_EQ(options.mode, Mode::kClient);
    EXPECT_EQ(options.join, "vocroz://join?code=ABCDE-12345");
    EXPECT_EQ(ok({"VOCROZ://join?code=X"}).mode, Mode::kClient);
}

TEST(LaunchOptionsTest, ParsesSettings) {
    const auto options = ok({"--data-dir=C:\\tmp\\v", "--lang=en", "--log-level=debug",
                             "--no-update-check", "--rendezvous-url=http://localhost:8080"});
    EXPECT_EQ(options.data_dir, "C:\\tmp\\v");
    EXPECT_EQ(options.lang, "en");
    EXPECT_EQ(options.log_level, common::LogLevel::kDebug);
    EXPECT_FALSE(options.update_check);
    EXPECT_EQ(options.rendezvous_url, "http://localhost:8080");
    EXPECT_EQ(ok({"--log-level=warn"}).log_level, common::LogLevel::kWarning);
}

TEST(LaunchOptionsTest, VersionAndHelpAreCommands) {
    EXPECT_EQ(ok({"--version"}).command, Command::kVersion);
    EXPECT_EQ(ok({"--help"}).command, Command::kHelp);
    EXPECT_EQ(ok({"--client", "--version"}).command, Command::kVersion);
}

TEST(LaunchOptionsTest, RejectsContradictions) {
    EXPECT_TRUE(fails({"--mode=host", "--join=ABCDE-12345"}));
    EXPECT_TRUE(fails({"--mode=host", "vocroz://join?code=ABCDE-12345"}));
    EXPECT_TRUE(fails({"--mode=host", "--client"}));
    EXPECT_TRUE(fails({"--join=A", "vocroz://join?code=B"}));
}

TEST(LaunchOptionsTest, RejectsUnknownAndInvalidValues) {
    EXPECT_TRUE(fails({"--unknown"}));
    EXPECT_TRUE(fails({"positional"}));
    EXPECT_TRUE(fails({"--mode=engine"}));
    EXPECT_TRUE(fails({"--lang=fr"}));
    EXPECT_TRUE(fails({"--log-level=trace"}));
    EXPECT_TRUE(fails({"--data-dir"}));
}

TEST(LaunchOptionsTest, RejectsDuplicates) {
    EXPECT_TRUE(fails({"--data-dir=a", "--data-dir=b"}));
    EXPECT_TRUE(fails({"--client", "--client"}));
    EXPECT_TRUE(fails({"--lang=ja", "--lang=en"}));
}

}  // namespace
}  // namespace vocroz::pa_desktop
