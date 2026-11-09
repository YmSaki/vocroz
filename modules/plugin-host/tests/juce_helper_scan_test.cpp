// SPDX-License-Identifier: AGPL-3.0-only
//
// scan_in_helper() の単体テスト。スキャン中に落ちる、止まる、壊れた出力を書くヘルパーで、
// 呼び出し側（このテストのプロセス）が落ちず、原因ごとの失敗が返ることを検証する。
// ヘルパーには tests/fake_plugin_scanner.cpp を使う。

#include <chrono>
#include <filesystem>

#include <gtest/gtest.h>

#include "vocroz/plugin_host/factories.h"

namespace vocroz::plugin_host {
namespace {

using namespace std::chrono_literals;

const std::filesystem::path kHelper{VOCROZ_FAKE_PLUGIN_SCANNER};

HelperScanError scan_error(const char* file, std::chrono::milliseconds timeout = 10s) {
    auto result = scan_in_helper(kHelper, file, timeout);
    EXPECT_FALSE(result.has_value()) << file;
    return result.has_value() ? HelperScanError::kLaunchFailed : result.error();
}

TEST(JuceHelperScanTest, ReturnsReportFromHelper) {
    auto result = scan_in_helper(kHelper, "Room Reverb.vst3", 10s);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().plugins.size(), 1U);
    EXPECT_EQ(result.value().plugins[0].name, "Room Reverb.vst3");
    EXPECT_EQ(result.value().plugins[0].file_path, "Room Reverb.vst3");
}

TEST(JuceHelperScanTest, ReportsCrashWithoutTakingDownTheCaller) {
    EXPECT_EQ(scan_error("crash"), HelperScanError::kCrashed);
    // 落ちたヘルパーの後も、次のファイルのスキャンを続けられる。
    EXPECT_TRUE(scan_in_helper(kHelper, "next.vst3", 10s).has_value());
}

TEST(JuceHelperScanTest, KillsHelperThatHangsPastTheTimeout) {
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(scan_error("hang", 500ms), HelperScanError::kTimedOut);
    // ヘルパーは 10 分止まるので、強制終了しなければここまで戻らない。
    EXPECT_LT(std::chrono::steady_clock::now() - start, 30s);
}

TEST(JuceHelperScanTest, RejectsOutputThatDoesNotMatchTheSchema) {
    EXPECT_EQ(scan_error("garbage"), HelperScanError::kInvalidOutput);
}

TEST(JuceHelperScanTest, TreatsExitWithoutOutputAsCrash) {
    EXPECT_EQ(scan_error("silent"), HelperScanError::kCrashed);
}

TEST(JuceHelperScanTest, ReportsMissingHelper) {
    auto result = scan_in_helper(kHelper.parent_path() / "no-such-helper", "x.vst3", 10s);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HelperScanError::kLaunchFailed);
}

}  // namespace
}  // namespace vocroz::plugin_host
