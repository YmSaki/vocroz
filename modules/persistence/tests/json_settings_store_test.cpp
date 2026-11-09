// SPDX-License-Identifier: AGPL-3.0-only
//
// JsonSettingsStore のテスト。保存の途中で止まった状態や壊れたファイルを実際のファイルで再現し、
// バックアップか既定値で読み込めること、それを使って起動したセッションが全ミュートで始まることを確かめる。

#include "json_settings_store.h"

#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "vocroz/session/client_session.h"
#include "vocroz/session/host_session.h"

#include "fakes/fake_capture_control.h"
#include "fakes/fake_clock.h"
#include "fakes/fake_host_link.h"
#include "fakes/fake_logger.h"
#include "fakes/fake_mixer_control.h"
#include "fakes/fake_output_device_control.h"
#include "fakes/fake_peer_link.h"
#include "fakes/fake_rendezvous_client.h"
#include "fakes/fake_secure_random.h"
#include "fakes/fake_ui_event_sink.h"
#include "settings_codec.h"
#include "support/temp_directory.h"

namespace vocroz::persistence {
namespace {

using session::SettingsError;

session::HostSettings host_settings(double gain_db) {
    session::HostSettings s;
    s.session_name = "合奏";
    s.output_device_id = "speaker-1";
    s.master_gain_db = gain_db;
    return s;
}

class JsonSettingsStoreTest : public ::testing::Test {
protected:
    std::filesystem::path host_path() const {
        return dir_.path() / "host-settings.json";
    }

    std::filesystem::path client_path() const {
        return dir_.path() / "client-settings.json";
    }

    bool logged(common::LogLevel level) const {
        for (const auto& entry : logger_.entries()) {
            if (entry.level == level) {
                return true;
            }
        }
        return false;
    }

    TempDirectory dir_;
    common::FakeLogger logger_;
    JsonSettingsStore store_{dir_.path(), logger_};
};

TEST_F(JsonSettingsStoreTest, ReportsNotFoundOnFirstLaunch) {
    const auto host = store_.load_host();
    ASSERT_FALSE(host);
    EXPECT_EQ(host.error(), SettingsError::kNotFound);
    const auto client = store_.load_client();
    ASSERT_FALSE(client);
    EXPECT_EQ(client.error(), SettingsError::kNotFound);
}

TEST_F(JsonSettingsStoreTest, LoadsWhatWasSaved) {
    ASSERT_TRUE(store_.save_host(host_settings(-3.0)));
    session::ClientSettings client;
    client.display_name = "Gt.";
    client.send_level_db = -12.0;
    ASSERT_TRUE(store_.save_client(client));

    JsonSettingsStore reopened{dir_.path(), logger_};
    ASSERT_TRUE(reopened.load_host());
    EXPECT_EQ(reopened.load_host().value(), host_settings(-3.0));
    ASSERT_TRUE(reopened.load_client());
    EXPECT_EQ(reopened.load_client().value(), client);
}

TEST_F(JsonSettingsStoreTest, HostAndClientUseSeparateFiles) {
    ASSERT_TRUE(store_.save_host(host_settings(0.0)));
    EXPECT_TRUE(std::filesystem::exists(host_path()));
    EXPECT_FALSE(std::filesystem::exists(client_path()));
}

TEST_F(JsonSettingsStoreTest, RestoresFromBackupWhenMainFileIsCorrupted) {
    ASSERT_TRUE(store_.save_host(host_settings(-1.0)));
    ASSERT_TRUE(store_.save_host(host_settings(-2.0)));
    write_text(host_path(), "{\"schema_version\":1,\"session_na");

    const auto loaded = store_.load_host();
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded.value(), host_settings(-1.0));
    EXPECT_TRUE(logged(common::LogLevel::kWarning));
}

TEST_F(JsonSettingsStoreTest, RestoresFromBackupWhenInterruptedBeforeFinalRename) {
    // 保存の 2 段目（本体をバックアップへ移す）の直後で止まった状態: 本体がなく、
    // バックアップに前回の内容、一時ファイルに今回の内容がある。
    write_text(dir_.path() / "host-settings.json.bak", encode_host_settings(host_settings(-4.0)));
    write_text(dir_.path() / "host-settings.json.tmp", encode_host_settings(host_settings(-5.0)));

    const auto loaded = store_.load_host();
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded.value(), host_settings(-4.0));
}

TEST_F(JsonSettingsStoreTest, IgnoresHalfWrittenTemporaryFile) {
    ASSERT_TRUE(store_.save_host(host_settings(-1.0)));
    write_text(dir_.path() / "host-settings.json.tmp", "{\"schema_ver");

    const auto loaded = store_.load_host();
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded.value(), host_settings(-1.0));
}

TEST_F(JsonSettingsStoreTest, ReportsCorruptedWhenMainAndBackupAreBroken) {
    write_text(host_path(), "not json");
    write_text(dir_.path() / "host-settings.json.bak", "");

    const auto loaded = store_.load_host();
    ASSERT_FALSE(loaded);
    EXPECT_EQ(loaded.error(), SettingsError::kCorrupted);
    EXPECT_TRUE(logged(common::LogLevel::kError));
}

TEST_F(JsonSettingsStoreTest, SavingOverCorruptedFileKeepsGoodBackup) {
    ASSERT_TRUE(store_.save_host(host_settings(-1.0)));
    ASSERT_TRUE(store_.save_host(host_settings(-2.0)));
    write_text(host_path(), "broken");

    ASSERT_TRUE(store_.save_host(host_settings(-3.0)));
    EXPECT_EQ(store_.load_host().value(), host_settings(-3.0));
    EXPECT_EQ(decode_host_settings(read_text(dir_.path() / "host-settings.json.bak")),
              host_settings(-1.0));
}

// --- 起動の初期ミュート ---

class SettingsCorruptionStartupTest : public JsonSettingsStoreTest {
protected:
    common::FakeClock clock_;
    session::FakeUiEventSink ui_;
};

TEST_F(SettingsCorruptionStartupTest, HostStartsWithMasterMutedFromBackup) {
    ASSERT_TRUE(store_.save_host(host_settings(-1.0)));
    ASSERT_TRUE(store_.save_host(host_settings(-2.0)));
    write_text(host_path(), std::string("\x00\x01garbage", 9));

    session::FakeMixerControl mixer;
    session::FakeOutputDeviceControl outputs;
    session::FakePeerLink peers;
    session::FakeRendezvousClient rendezvous;
    session::FakeSecureRandom random;
    session::HostSession host{
        {}, {ui_, mixer, outputs, peers, rendezvous, store_, random, clock_, logger_}};
    host.start();

    EXPECT_TRUE(mixer.master_muted());
    EXPECT_EQ(mixer.master_gain_db(), control_protocol::GainDb{-1.0});
}

TEST_F(SettingsCorruptionStartupTest, HostStartsWithMasterMutedAndDefaultsWhenNothingIsReadable) {
    write_text(host_path(), "garbage");
    write_text(dir_.path() / "host-settings.json.bak", "garbage");

    session::FakeMixerControl mixer;
    session::FakeOutputDeviceControl outputs;
    session::FakePeerLink peers;
    session::FakeRendezvousClient rendezvous;
    session::FakeSecureRandom random;
    session::HostSession host{
        {}, {ui_, mixer, outputs, peers, rendezvous, store_, random, clock_, logger_}};
    host.start();

    EXPECT_TRUE(mixer.master_muted());
    EXPECT_EQ(mixer.master_gain_db(), session::HostSettings{}.master_gain_db);
}

TEST_F(SettingsCorruptionStartupTest, ClientStartsWithSendMutedWhenSettingsAreCorrupted) {
    write_text(client_path(), "{\"schema_version\":1,\"display_name\":\"Vo\",\"sel");

    session::FakeCaptureControl capture;
    session::FakeHostLink link;
    session::FakeRendezvousClient rendezvous;
    session::ClientSession client{{ui_, capture, link, rendezvous, store_, clock_, logger_}};
    client.start();

    EXPECT_TRUE(capture.send_muted());
}

}  // namespace
}  // namespace vocroz::persistence
