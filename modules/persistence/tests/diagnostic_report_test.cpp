// SPDX-License-Identifier: AGPL-3.0-only
//
// write_diagnostic_report() のテスト。表示名とセッション名を値ごと伏せ、ログの秘密情報を伏せ、
// 壊れた設定やログがなくてもレポートを書けることを確かめる。

#include "vocroz/persistence/diagnostic_report.h"

#include <chrono>
#include <string>

#include <gtest/gtest.h>

#include "fakes/fake_logger.h"
#include "json_settings_store.h"
#include "support/temp_directory.h"

namespace vocroz::persistence {
namespace {

class DiagnosticReportTest : public ::testing::Test {
protected:
    DiagnosticReportInput input() const {
        using namespace std::chrono;
        DiagnosticReportInput in;
        in.created_at = sys_days{2026y / November / 8};
        in.product_version = "VOCROZ 0.1.0";
        in.environment = {"OS: Windows 11 23H2", R"(data: C:\Users\taro\AppData\Local\VOCROZ)"};
        in.data_dir = dir_.path();
        in.log_dir = dir_.path() / "logs";
        return in;
    }

    std::string write_report() const {
        const auto output = dir_.path() / "report.txt";
        EXPECT_TRUE(write_diagnostic_report(input(), output));
        return read_text(output);
    }

    static bool contains(const std::string& text, std::string_view part) {
        return text.find(part) != std::string::npos;
    }

    TempDirectory dir_;
    common::FakeLogger logger_;
};

TEST_F(DiagnosticReportTest, IncludesVersionEnvironmentAndCreationTime) {
    const auto report = write_report();
    EXPECT_TRUE(contains(report, "VOCROZ 0.1.0"));
    EXPECT_TRUE(contains(report, "OS: Windows 11 23H2"));
    EXPECT_TRUE(contains(report, "2026-11-08T00:00:00.000Z"));
    EXPECT_TRUE(contains(report, R"(C:\Users\<user>\AppData)"));
    EXPECT_FALSE(contains(report, "taro"));
}

TEST_F(DiagnosticReportTest, MasksNamesButKeepsOtherSettings) {
    JsonSettingsStore store{dir_.path(), logger_};
    session::HostSettings host;
    host.session_name = "山田家の合奏";
    host.output_device_id = "speaker-1";
    host.master_gain_db = -6.0;
    host.routing.buses.push_back(
        control_protocol::RoutingBus{"bus-1", "山田さんの返し", control_protocol::BusKind::kCue});
    host.routing.outputs.push_back(control_protocol::RoutingOutput{"out-1", "山田 IEM", 2});
    ASSERT_TRUE(store.save_host(host));
    session::ClientSettings client;
    client.display_name = "Vo. 山田";
    ASSERT_TRUE(store.save_client(client));

    const auto report = write_report();
    EXPECT_FALSE(contains(report, "山田"));
    EXPECT_TRUE(contains(report, "<masked>"));
    EXPECT_TRUE(contains(report, "speaker-1"));
    EXPECT_TRUE(contains(report, "-6.0"));
    EXPECT_TRUE(contains(report, "bus-1"));
}

TEST_F(DiagnosticReportTest, DescribesCorruptedAndMissingSettings) {
    write_text(dir_.path() / "host-settings.json", "garbage");
    const auto report = write_report();
    EXPECT_TRUE(contains(report, "壊れています。正常なバックアップはありません"));
    EXPECT_TRUE(contains(report, "（ありません）"));
}

TEST_F(DiagnosticReportTest, IncludesMaskedLogsOldestFirst) {
    std::filesystem::create_directories(dir_.path() / "logs");
    write_text(dir_.path() / "logs" / "pa-engine.1.log", "old line\n");
    write_text(dir_.path() / "logs" / "pa-engine.log",
               "new line peer 198.51.100.4 code ABCDE-12345\n");
    // 更新時刻で新旧を決めるので、明示的にずらす。
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(dir_.path() / "logs" / "pa-engine.1.log",
                                     now - std::chrono::hours{1});
    std::filesystem::last_write_time(dir_.path() / "logs" / "pa-engine.log", now);
    write_text(dir_.path() / "logs" / "notes.txt", "not a log");

    const auto report = write_report();
    const auto old_at = report.find("old line");
    const auto new_at = report.find("new line");
    ASSERT_NE(old_at, std::string::npos);
    ASSERT_NE(new_at, std::string::npos);
    EXPECT_LT(old_at, new_at);
    EXPECT_FALSE(contains(report, "198.51.100.4"));
    EXPECT_FALSE(contains(report, "ABCDE-12345"));
    EXPECT_FALSE(contains(report, "not a log"));
}

TEST_F(DiagnosticReportTest, WritesReportWithoutLogs) {
    const auto report = write_report();
    EXPECT_TRUE(contains(report, "== ログ ==\n（ありません）"));
}

TEST_F(DiagnosticReportTest, FailsWhenOutputCannotBeWritten) {
    const auto result = write_diagnostic_report(input(), dir_.path() / "missing" / "report.txt");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), DiagnosticReportError::kWriteFailed);
}

}  // namespace
}  // namespace vocroz::persistence
