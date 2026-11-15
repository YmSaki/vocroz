// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機のセッションの録音とメディアプレイヤーの単体テスト
// （docs/adr/0043-recording-and-media-player.md）。
// 起動時は録音も再生も止めた状態から始めること、録る対象とファイル名の決め方、録音中は設定を
// 変えさせないこと、書き出しの失敗で全体を止めて知らせること、ライブラリにあるファイルだけを
// 読み込むこと、流す先がなくなったら一時停止して知らせることを確かめる。

#include <chrono>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/session/host_session.h"

#include "fakes/fake_clock.h"
#include "fakes/fake_external_output_control.h"
#include "fakes/fake_logger.h"
#include "fakes/fake_media_library.h"
#include "fakes/fake_media_player_control.h"
#include "fakes/fake_mixer_control.h"
#include "fakes/fake_output_device_control.h"
#include "fakes/fake_peer_link.h"
#include "fakes/fake_playout_control.h"
#include "fakes/fake_plugin_rack.h"
#include "fakes/fake_plugin_scan_control.h"
#include "fakes/fake_recorder_control.h"
#include "fakes/fake_rendezvous_client.h"
#include "fakes/fake_secret_store.h"
#include "fakes/fake_secure_random.h"
#include "fakes/fake_settings_store.h"
#include "fakes/fake_ui_event_sink.h"

namespace vocroz::session {
namespace {

namespace cp = control_protocol;
using cp::ErrorCode;
using cp::MediaTransport;
using cp::RecorderState;
using namespace std::chrono_literals;

constexpr const char* kFolder = "C:/Users/me/Music/VOCROZ";
constexpr const char* kRecorder = "recorder";
constexpr const char* kPlayer = "media_player";

cp::RoutingConfig bus_routing(std::string name = "配信") {
    cp::RoutingConfig config;
    config.buses.push_back(cp::RoutingBus{"bus-stream", std::move(name), cp::BusKind::kUser});
    config.sends.push_back(
        cp::RoutingSend{"send-1", "master", "bus-stream", cp::SendTap::kPostFader});
    return config;
}

cp::RecorderConfig sources(std::vector<std::string> ids, std::string folder = {}) {
    cp::RecorderConfig config;
    config.folder = std::move(folder);
    config.sources = std::move(ids);
    return config;
}

class HostMediaTest : public ::testing::Test {
protected:
    HostMediaTest() {
        library_.set_files(kFolder, {"20261101-190000/01-Master.wav", "bgm/opening.flac"});
    }

    HostSession& session() {
        if (!session_) {
            HostSession::Config config;
            config.default_media_folder = kFolder;
            session_.emplace(
                std::move(config),
                HostSession::Ports{ui_, mixer_, outputs_, peers_, rendezvous_, settings_, random_,
                                   clock_, logger_, playout_, external_output_, secrets_, plugins_,
                                   plugin_scan_, recorder_, player_, library_});
            session_->start();
        }
        return *session_;
    }

    cp::Recorder recorder() {
        return session().snapshot().recorder;
    }
    cp::MediaPlayer player() {
        return session().snapshot().media_player;
    }

    // 離散コマンドを送り、Ack なら revision、Reject なら std::nullopt を返す。
    std::optional<cp::Revision> command(const std::string& entity_id,
                                        cp::DiscreteCommandBody body) {
        const auto request_id = "r" + std::to_string(++request_number_);
        const auto snapshot = session().snapshot();
        cp::Revision revision = 0;
        if (entity_id == "routing") {
            revision = snapshot.routing.revision;
        } else if (entity_id == kRecorder) {
            revision = snapshot.recorder.revision;
        } else {
            revision = snapshot.media_player.revision;
        }
        session().handle_ui_message(
            cp::DiscreteCommand{request_id, entity_id, revision, std::move(body)});
        last_reject_.reset();
        for (auto it = ui_.messages().rbegin(); it != ui_.messages().rend(); ++it) {
            if (const auto* ack = std::get_if<cp::Ack>(&*it);
                ack && ack->request_id == request_id) {
                return ack->applied_revision;
            }
            if (const auto* rej = std::get_if<cp::Reject>(&*it);
                rej && rej->request_id == request_id) {
                last_reject_ = rej->error_code;
                return std::nullopt;
            }
        }
        ADD_FAILURE() << "応答がない: " << request_id;
        return std::nullopt;
    }

    void apply_routing(const cp::RoutingConfig& config) {
        ASSERT_TRUE(command("routing", cp::StageRouting{config}));
        ASSERT_TRUE(command("routing", cp::CommitRouting{false}));
    }

    template <typename T>
    std::vector<T> published() const {
        std::vector<T> found;
        for (const auto& message : ui_.messages()) {
            if (const auto* m = std::get_if<T>(&message)) {
                found.push_back(*m);
            }
        }
        return found;
    }

    bool noticed(const std::string& code) const {
        for (const auto& n : published<cp::Notice>()) {
            if (n.code == code) {
                return true;
            }
        }
        return false;
    }

    // ファイルを読み込んで再生を始める。
    void play_opening() {
        ASSERT_TRUE(command(kPlayer, cp::LoadMedia{"bgm/opening.flac"}));
        ASSERT_TRUE(command(kPlayer, cp::SetMediaTransport{MediaTransport::kPlaying}));
    }

    FakeUiEventSink ui_;
    FakeMixerControl mixer_;
    FakeOutputDeviceControl outputs_;
    FakePlayoutControl playout_;
    FakePeerLink peers_;
    FakeRendezvousClient rendezvous_;
    FakeSettingsStore settings_;
    FakeSecureRandom random_;
    FakeExternalOutputControl external_output_;
    FakeSecretStore secrets_;
    FakePluginRack plugins_;
    FakePluginScanControl plugin_scan_;
    FakeRecorderControl recorder_;
    FakeMediaPlayerControl player_;
    FakeMediaLibrary library_;
    common::FakeClock clock_;
    common::FakeLogger logger_;
    std::optional<HostSession> session_;
    int request_number_ = 0;
    std::optional<ErrorCode> last_reject_;
};

// --- 起動時 ---

TEST_F(HostMediaTest, StartsStoppedWithDefaultFolderAndLibrary) {
    const auto snapshot = session().snapshot();
    EXPECT_EQ(snapshot.recorder.entity_id, kRecorder);
    EXPECT_EQ(snapshot.recorder.state, RecorderState::kStopped);
    EXPECT_EQ(snapshot.recorder.config.folder, kFolder);
    EXPECT_TRUE(snapshot.recorder.config.sources.empty());
    EXPECT_EQ(snapshot.media_player.entity_id, kPlayer);
    EXPECT_EQ(snapshot.media_player.transport, MediaTransport::kStopped);
    EXPECT_FALSE(snapshot.media_player.file);
    EXPECT_EQ(snapshot.media_player.target, "master");
    ASSERT_EQ(snapshot.media_library.size(), 2U);
    EXPECT_EQ(snapshot.media_library[1].path, "bgm/opening.flac");
    EXPECT_TRUE(recorder_.starts().empty());
    EXPECT_FALSE(player_.playing());
}

TEST_F(HostMediaTest, RestoresSavedSourcesThatStillExist) {
    // 保存したバスがルーティングにない（壊れた設定や古い設定）なら、録る対象から外す。
    HostSettings saved;
    saved.recorder = sources({"master", "bus-gone"});
    saved.recorder.format = cp::RecordingFormat::kFlac;
    settings_.host() = saved;

    const auto info = recorder();
    EXPECT_EQ(info.config.sources, std::vector<std::string>{"master"});
    EXPECT_EQ(info.config.format, cp::RecordingFormat::kFlac);
    EXPECT_EQ(info.config.folder, kFolder);
    EXPECT_EQ(info.state, RecorderState::kStopped);
}

// --- 録音の設定 ---

TEST_F(HostMediaTest, RejectsUnknownAndDuplicateSources) {
    EXPECT_FALSE(command(kRecorder, cp::SetRecorderConfig{sources({"master", "ch-unknown"})}));
    EXPECT_EQ(last_reject_, ErrorCode::kNotFound);
    EXPECT_FALSE(command(kRecorder, cp::SetRecorderConfig{sources({"master", "master"})}));
    EXPECT_EQ(last_reject_, ErrorCode::kNotFound);
    EXPECT_TRUE(recorder().config.sources.empty());
}

TEST_F(HostMediaTest, SavesSourcesAndLeavesDefaultFolderUnsaved) {
    apply_routing(bus_routing());
    ASSERT_TRUE(command(kRecorder, cp::SetRecorderConfig{sources({"master", "bus-stream"})}));

    ASSERT_TRUE(settings_.host());
    EXPECT_EQ(settings_.host()->recorder.sources,
              (std::vector<std::string>{"master", "bus-stream"}));
    // 既定のフォルダは保存しない（ユーザーの音楽フォルダの場所が変わっても追従させる）。
    EXPECT_TRUE(settings_.host()->recorder.folder.empty());
}

TEST_F(HostMediaTest, ChangingFolderRebuildsLibrary) {
    library_.set_files("D:/Takes", {"a.wav"});
    ASSERT_TRUE(command(kRecorder, cp::SetRecorderConfig{sources({"master"}, "D:/Takes")}));

    const auto snapshot = session().snapshot();
    ASSERT_EQ(snapshot.media_library.size(), 1U);
    EXPECT_EQ(snapshot.media_library[0].path, "a.wav");
    EXPECT_FALSE(published<cp::MediaLibraryUpdated>().empty());
    EXPECT_EQ(settings_.host()->recorder.folder, "D:/Takes");
}

TEST_F(HostMediaTest, RemovedBusIsDroppedFromSources) {
    apply_routing(bus_routing());
    ASSERT_TRUE(command(kRecorder, cp::SetRecorderConfig{sources({"master", "bus-stream"})}));
    apply_routing(cp::RoutingConfig{});
    // ID を振り直したバスを、別の対象として録ってしまわないため。
    EXPECT_EQ(recorder().config.sources, std::vector<std::string>{"master"});
}

// --- 録音 ---

TEST_F(HostMediaTest, StartBuildsNumberedSanitizedFileNames) {
    apply_routing(bus_routing("配信/メイン"));
    ASSERT_TRUE(command(kRecorder, cp::SetRecorderConfig{sources({"master", "bus-stream"})}));
    ASSERT_TRUE(command(kRecorder, cp::SetRecording{true}));

    ASSERT_EQ(recorder_.starts().size(), 1U);
    const auto& plan = recorder_.starts()[0];
    EXPECT_EQ(plan.folder, kFolder);
    EXPECT_EQ(plan.format, audio_core::AudioFileFormat::kWav);
    ASSERT_EQ(plan.tracks.size(), 2U);
    EXPECT_EQ(plan.tracks[0], (RecordingTrackPlan{"master", "01-Master.wav"}));
    // Windows でファイル名に使えない "/" は "_" にする。
    EXPECT_EQ(plan.tracks[1], (RecordingTrackPlan{"bus-stream", "02-配信_メイン.wav"}));

    const auto info = recorder();
    EXPECT_EQ(info.state, RecorderState::kRecording);
    EXPECT_EQ(info.take, "20261115-120000");
    EXPECT_EQ(info.last_result, cp::RecorderResult::kOk);
    ASSERT_EQ(info.tracks.size(), 2U);
    EXPECT_EQ(info.tracks[1].file_name, "02-配信_メイン.wav");
}

TEST_F(HostMediaTest, FlacUsesFlacExtension) {
    auto config = sources({"master"});
    config.format = cp::RecordingFormat::kFlac;
    ASSERT_TRUE(command(kRecorder, cp::SetRecorderConfig{config}));
    ASSERT_TRUE(command(kRecorder, cp::SetRecording{true}));
    ASSERT_EQ(recorder_.starts().size(), 1U);
    EXPECT_EQ(recorder_.starts()[0].format, audio_core::AudioFileFormat::kFlac);
    EXPECT_EQ(recorder_.starts()[0].tracks[0].file_name, "01-Master.flac");
}

TEST_F(HostMediaTest, StartWithoutSourcesIsRejected) {
    EXPECT_FALSE(command(kRecorder, cp::SetRecording{true}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
    EXPECT_TRUE(recorder_.starts().empty());
}

TEST_F(HostMediaTest, StartFailureStaysStoppedWithResult) {
    ASSERT_TRUE(command(kRecorder, cp::SetRecorderConfig{sources({"master"})}));
    recorder_.fail_start(cp::RecorderResult::kFolderUnavailable);
    // ファイルを開けなかったこともコマンドとしては受け付け、結果で原因を知らせる。
    ASSERT_TRUE(command(kRecorder, cp::SetRecording{true}));

    const auto info = recorder();
    EXPECT_EQ(info.state, RecorderState::kStopped);
    EXPECT_EQ(info.last_result, cp::RecorderResult::kFolderUnavailable);
    EXPECT_FALSE(info.take);
    EXPECT_TRUE(info.tracks.empty());
}

TEST_F(HostMediaTest, ConfigCannotChangeWhileRecording) {
    ASSERT_TRUE(command(kRecorder, cp::SetRecorderConfig{sources({"master"})}));
    ASSERT_TRUE(command(kRecorder, cp::SetRecording{true}));
    auto config = sources({"master"});
    config.format = cp::RecordingFormat::kFlac;
    EXPECT_FALSE(command(kRecorder, cp::SetRecorderConfig{config}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
}

TEST_F(HostMediaTest, PublishesElapsedAndDroppedEverySecond) {
    ASSERT_TRUE(command(kRecorder, cp::SetRecorderConfig{sources({"master"})}));
    ASSERT_TRUE(command(kRecorder, cp::SetRecording{true}));
    const auto revision = recorder().revision;
    const auto before = published<cp::RecorderUpdated>().size();

    recorder_.set_dropped(0, 480);  // 10 ms
    clock_.advance(500ms);
    session().tick();
    EXPECT_EQ(published<cp::RecorderUpdated>().size(), before);

    clock_.advance(600ms);
    session().tick();
    const auto updates = published<cp::RecorderUpdated>();
    ASSERT_EQ(updates.size(), before + 1);
    EXPECT_EQ(updates.back().recorder.elapsed_ms, 1100U);
    EXPECT_EQ(updates.back().recorder.tracks[0].dropped_ms, 10U);
    // 経過時間と欠落は観測値なので revision は進めない。
    EXPECT_EQ(updates.back().recorder.revision, revision);
}

TEST_F(HostMediaTest, WriteFailureStopsRecordingAndNotifies) {
    ASSERT_TRUE(command(kRecorder, cp::SetRecorderConfig{sources({"master"})}));
    ASSERT_TRUE(command(kRecorder, cp::SetRecording{true}));
    const auto revision = recorder().revision;

    recorder_.fail(audio_core::AudioFileError::kDiskFull);
    session().tick();

    EXPECT_EQ(recorder_.stops(), 1);
    const auto info = recorder();
    EXPECT_EQ(info.state, RecorderState::kStopped);
    EXPECT_EQ(info.last_result, cp::RecorderResult::kDiskFull);
    EXPECT_GT(info.revision, revision);
    // 録れたところまでのテイクは残るので、テイクとトラックの表示も残す。
    EXPECT_EQ(info.take, "20261115-120000");
    EXPECT_EQ(info.tracks.size(), 1U);
    EXPECT_TRUE(noticed("recording_failed"));
    // ライブの音声は録音と関係なく流れ続ける（Master のミュートを変えない）。
    EXPECT_EQ(session().snapshot().master.master_muted, true);
}

TEST_F(HostMediaTest, StopRefreshesLibrary) {
    ASSERT_TRUE(command(kRecorder, cp::SetRecorderConfig{sources({"master"})}));
    ASSERT_TRUE(command(kRecorder, cp::SetRecording{true}));
    library_.set_files(kFolder, {"20261115-120000/01-Master.wav"});
    clock_.advance(3s);
    ASSERT_TRUE(command(kRecorder, cp::SetRecording{false}));

    EXPECT_EQ(recorder_.stops(), 1);
    const auto snapshot = session().snapshot();
    EXPECT_EQ(snapshot.recorder.state, RecorderState::kStopped);
    EXPECT_EQ(snapshot.recorder.elapsed_ms, 3000U);
    ASSERT_EQ(snapshot.media_library.size(), 1U);
    EXPECT_EQ(snapshot.media_library[0].path, "20261115-120000/01-Master.wav");
}

// --- メディアプレイヤー ---

TEST_F(HostMediaTest, LoadsOnlyFilesInLibrary) {
    // UI から任意のパスを受け取らない。
    EXPECT_FALSE(command(kPlayer, cp::LoadMedia{"../../Windows/win.ini"}));
    EXPECT_EQ(last_reject_, ErrorCode::kNotFound);
    EXPECT_TRUE(player_.loads().empty());

    ASSERT_TRUE(command(kPlayer, cp::LoadMedia{"bgm/opening.flac"}));
    ASSERT_EQ(player_.loads().size(), 1U);
    EXPECT_EQ(player_.loads()[0], std::string{kFolder} + "/bgm/opening.flac");
    EXPECT_EQ(player_.target(), "master");

    const auto info = player();
    EXPECT_EQ(info.file, "bgm/opening.flac");
    EXPECT_EQ(info.channels, 2U);
    EXPECT_EQ(info.duration_ms, 10'000U);
    EXPECT_EQ(info.last_result, cp::MediaResult::kOk);
    // 読み込んだ直後は鳴らさない（初期ミュート）。
    EXPECT_EQ(info.transport, MediaTransport::kStopped);
    EXPECT_FALSE(player_.playing());
}

TEST_F(HostMediaTest, LoadFailureLeavesNoFile) {
    player_.fail_load(std::string{kFolder} + "/bgm/opening.flac",
                      cp::MediaResult::kUnsupportedFormat);
    ASSERT_TRUE(command(kPlayer, cp::LoadMedia{"bgm/opening.flac"}));
    const auto info = player();
    EXPECT_FALSE(info.file);
    EXPECT_EQ(info.last_result, cp::MediaResult::kUnsupportedFormat);
    EXPECT_FALSE(command(kPlayer, cp::SetMediaTransport{MediaTransport::kPlaying}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
}

TEST_F(HostMediaTest, EmptyPathUnloads) {
    ASSERT_TRUE(command(kPlayer, cp::LoadMedia{"bgm/opening.flac"}));
    ASSERT_TRUE(command(kPlayer, cp::LoadMedia{""}));
    EXPECT_EQ(player_.unloads(), 1);
    EXPECT_FALSE(player().file);
}

TEST_F(HostMediaTest, PlayPauseStopAndSeek) {
    play_opening();
    EXPECT_TRUE(player_.playing());
    EXPECT_EQ(player().transport, MediaTransport::kPlaying);

    ASSERT_TRUE(command(kPlayer, cp::SetMediaTransport{MediaTransport::kPaused}));
    EXPECT_FALSE(player_.playing());

    ASSERT_TRUE(command(kPlayer, cp::SeekMedia{2'500}));
    EXPECT_EQ(player_.seeks().back(), 2'500U * 48U);
    EXPECT_EQ(player().position_ms, 2'500U);

    // 長さを超える位置は終わりとして扱う。
    ASSERT_TRUE(command(kPlayer, cp::SeekMedia{60'000}));
    EXPECT_EQ(player_.seeks().back(), 480'000U);

    ASSERT_TRUE(command(kPlayer, cp::SetMediaTransport{MediaTransport::kStopped}));
    EXPECT_EQ(player_.seeks().back(), 0U);
    EXPECT_EQ(player().position_ms, 0U);
}

TEST_F(HostMediaTest, PublishesPositionWhilePlaying) {
    play_opening();
    const auto revision = player().revision;
    const auto before = published<cp::MediaPlayerUpdated>().size();

    player_.set_position(48'000);
    clock_.advance(100ms);
    session().tick();
    EXPECT_EQ(published<cp::MediaPlayerUpdated>().size(), before);

    clock_.advance(200ms);
    session().tick();
    const auto updates = published<cp::MediaPlayerUpdated>();
    ASSERT_EQ(updates.size(), before + 1);
    EXPECT_EQ(updates.back().media_player.position_ms, 1'000U);
    EXPECT_EQ(updates.back().media_player.revision, revision);
}

TEST_F(HostMediaTest, EndOfFileStopsAtStartWithoutRevision) {
    play_opening();
    const auto revision = player().revision;
    player_.reach_end();
    session().tick();

    const auto info = player();
    EXPECT_EQ(info.transport, MediaTransport::kStopped);
    EXPECT_EQ(info.position_ms, 0U);
    EXPECT_EQ(info.revision, revision);
    EXPECT_FALSE(player_.playing());
    EXPECT_EQ(player_.seeks().back(), 0U);
}

TEST_F(HostMediaTest, ReadFailurePausesAndNotifies) {
    play_opening();
    player_.fail(audio_core::AudioFileError::kIoError);
    session().tick();

    const auto info = player();
    EXPECT_EQ(info.transport, MediaTransport::kPaused);
    EXPECT_EQ(info.last_result, cp::MediaResult::kIoError);
    EXPECT_FALSE(player_.playing());
    EXPECT_TRUE(noticed("media_player_failed"));
}

TEST_F(HostMediaTest, TargetMustBeMasterOrBus) {
    EXPECT_FALSE(command(kPlayer, cp::SetMediaTarget{"bus-stream"}));
    EXPECT_EQ(last_reject_, ErrorCode::kNotFound);

    apply_routing(bus_routing());
    ASSERT_TRUE(command(kPlayer, cp::SetMediaTarget{"bus-stream"}));
    EXPECT_EQ(player_.target(), "bus-stream");
    EXPECT_EQ(player().target, "bus-stream");
}

TEST_F(HostMediaTest, RemovedTargetPausesWithoutSwitching) {
    apply_routing(bus_routing());
    ASSERT_TRUE(command(kPlayer, cp::SetMediaTarget{"bus-stream"}));
    play_opening();

    apply_routing(cp::RoutingConfig{});

    const auto info = player();
    EXPECT_EQ(info.transport, MediaTransport::kPaused);
    // 別のバスや Master へ黙って切り替えない。
    EXPECT_EQ(info.target, "bus-stream");
    EXPECT_FALSE(player_.playing());
    EXPECT_TRUE(noticed("media_player_target_removed"));

    // 流す先がないまま再生は始めさせない。
    EXPECT_FALSE(command(kPlayer, cp::SetMediaTransport{MediaTransport::kPlaying}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
}

TEST_F(HostMediaTest, CommandsToOtherEntitiesAreRejected) {
    EXPECT_FALSE(command(kPlayer, cp::SetRecording{true}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
    EXPECT_FALSE(command(kRecorder, cp::LoadMedia{"bgm/opening.flac"}));
    EXPECT_EQ(last_reject_, ErrorCode::kInvalidState);
}

TEST_F(HostMediaTest, RefreshLibraryPublishesFiles) {
    library_.set_files(kFolder, {"new.wav"});
    ASSERT_TRUE(command(kPlayer, cp::RefreshMediaLibrary{}));
    const auto updates = published<cp::MediaLibraryUpdated>();
    ASSERT_FALSE(updates.empty());
    ASSERT_EQ(updates.back().files.size(), 1U);
    EXPECT_EQ(updates.back().files[0].path, "new.wav");
}

}  // namespace
}  // namespace vocroz::session
