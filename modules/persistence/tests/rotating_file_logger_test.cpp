// SPDX-License-Identifier: AGPL-3.0-only
//
// RotatingFileLogger のテスト。1 行の書式、重要度の絞り込み、秘密情報の伏せ字、
// 改行の除去、大きさによるローテーションと残すファイルの数を確かめる。

#include "rotating_file_logger.h"

#include <algorithm>
#include <chrono>
#include <string>

#include <gtest/gtest.h>

#include "support/temp_directory.h"

namespace vocroz::persistence {
namespace {

using common::LogLevel;

std::chrono::system_clock::time_point fixed_time() {
    using namespace std::chrono;
    return sys_days{2026y / November / 8} + 1h + 2min + 3s + 4ms;
}

class RotatingFileLoggerTest : public ::testing::Test {
protected:
    LogOptions options(std::uintmax_t max_bytes = 1024 * 1024, int count = 5) const {
        LogOptions o;
        o.directory = dir_.path() / "logs";
        o.base_name = "pa-engine";
        o.min_level = LogLevel::kInfo;
        o.max_file_bytes = max_bytes;
        o.file_count = count;
        return o;
    }

    std::filesystem::path log_file(const std::string& name) const {
        return dir_.path() / "logs" / name;
    }

    TempDirectory dir_;
};

TEST_F(RotatingFileLoggerTest, WritesOneLinePerEntryWithUtcTimeAndLevel) {
    RotatingFileLogger logger{options(), fixed_time};
    logger.log(LogLevel::kInfo, "起動しました");
    logger.log(LogLevel::kError, "出力デバイスを開けません");
    EXPECT_EQ(read_text(log_file("pa-engine.log")),
              "2026-11-08T01:02:03.004Z [INFO] 起動しました\n"
              "2026-11-08T01:02:03.004Z [ERROR] 出力デバイスを開けません\n");
}

TEST_F(RotatingFileLoggerTest, DropsEntriesBelowMinimumLevel) {
    RotatingFileLogger logger{options(), fixed_time};
    logger.log(LogLevel::kDebug, "詳細");
    logger.log(LogLevel::kWarning, "注意");
    const auto text = read_text(log_file("pa-engine.log"));
    EXPECT_EQ(text.find("詳細"), std::string::npos);
    EXPECT_NE(text.find("[WARN] 注意"), std::string::npos);
}

TEST_F(RotatingFileLoggerTest, MasksSecretsBeforeWriting) {
    RotatingFileLogger logger{options(), fixed_time};
    logger.log(LogLevel::kInfo, "招待 ABCDE-12345 を発行 token=00112233445566778899aabbccddeeff00");
    const auto text = read_text(log_file("pa-engine.log"));
    EXPECT_EQ(text.find("ABCDE"), std::string::npos);
    EXPECT_EQ(text.find("00112233"), std::string::npos);
    EXPECT_NE(text.find("招待 <invite-code> を発行 token=<secret>"), std::string::npos);
}

TEST_F(RotatingFileLoggerTest, ReplacesNewlinesSoEntriesCannotForgeLines) {
    RotatingFileLogger logger{options(), fixed_time};
    logger.log(LogLevel::kInfo, "名前\n2026-01-01T00:00:00.000Z [ERROR] 偽の行");
    const auto text = read_text(log_file("pa-engine.log"));
    EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 1);
}

TEST_F(RotatingFileLoggerTest, AppendsToExistingFile) {
    {
        RotatingFileLogger logger{options(), fixed_time};
        logger.log(LogLevel::kInfo, "1 回目");
    }
    RotatingFileLogger logger{options(), fixed_time};
    logger.log(LogLevel::kInfo, "2 回目");
    const auto text = read_text(log_file("pa-engine.log"));
    EXPECT_NE(text.find("1 回目"), std::string::npos);
    EXPECT_NE(text.find("2 回目"), std::string::npos);
}

TEST_F(RotatingFileLoggerTest, RotatesWhenFileWouldExceedLimitAndKeepsConfiguredCount) {
    // 1 行は 40 バイト前後なので、上限 100 バイトでは 2 行ごとに次のファイルへ移る。
    RotatingFileLogger logger{options(100, 3), fixed_time};
    for (int i = 0; i < 10; ++i) {
        logger.log(LogLevel::kInfo, "line " + std::to_string(i));
    }
    EXPECT_TRUE(std::filesystem::exists(log_file("pa-engine.log")));
    EXPECT_TRUE(std::filesystem::exists(log_file("pa-engine.1.log")));
    EXPECT_TRUE(std::filesystem::exists(log_file("pa-engine.2.log")));
    EXPECT_FALSE(std::filesystem::exists(log_file("pa-engine.3.log")));
    for (const auto* name : {"pa-engine.log", "pa-engine.1.log", "pa-engine.2.log"}) {
        EXPECT_LE(std::filesystem::file_size(log_file(name)), 100u) << name;
    }
    // 一番新しい行は現在のファイルに、古い行は消えている。
    EXPECT_NE(read_text(log_file("pa-engine.log")).find("line 9"), std::string::npos);
    EXPECT_EQ(read_text(log_file("pa-engine.2.log")).find("line 0"), std::string::npos);
}

TEST_F(RotatingFileLoggerTest, KeepsOnlyCurrentFileWhenCountIsOne) {
    RotatingFileLogger logger{options(100, 1), fixed_time};
    for (int i = 0; i < 6; ++i) {
        logger.log(LogLevel::kInfo, "line " + std::to_string(i));
    }
    EXPECT_FALSE(std::filesystem::exists(log_file("pa-engine.1.log")));
    EXPECT_NE(read_text(log_file("pa-engine.log")).find("line 5"), std::string::npos);
}

TEST_F(RotatingFileLoggerTest, SilentlyDropsEntriesWhenDirectoryCannotBeCreated) {
    // ログのフォルダの場所に同名のファイルがあると、フォルダを作れない。
    write_text(dir_.path() / "logs", "not a directory");
    RotatingFileLogger logger{options(), fixed_time};
    logger.log(LogLevel::kError, "書けない");  // 落ちずに戻ればよい
    SUCCEED();
}

}  // namespace
}  // namespace vocroz::persistence
