// SPDX-License-Identifier: AGPL-3.0-only
//
// EngineRecorder の単体テスト。本物の Mixer と EngineMixer に録音タップをつなぎ、テストの
// スレッドをオーディオスレッドに見立てて render() を回す。
// ディスクの書き込みが止まっても、ディスクがいっぱいになっても render() が待たずに返ること、
// 欠落したぶんを無音で埋めてトラックの長さがそろうこと、ストリップはプリフェーダーで録ること、
// 止めた後はグラフが落ち着いてからファイルを閉じることを確かめる（AGENTS.md 第 9 節）。

#include "engine_recorder.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/audio_core/mixer.h"

#include "engine_constants.h"
#include "fakes/fake_audio_file_writer.h"
#include "fakes/fake_audio_source.h"
#include "fakes/fake_clock.h"
#include "fakes/fake_logger.h"
#include "support/temp_directory.h"

namespace vocroz::engine {
namespace {

namespace ac = audio_core;
namespace cp = control_protocol;
using namespace std::chrono_literals;

constexpr std::size_t kCallbackFrames = 1024;
constexpr float kInput = 0.1F;

/// テストが持つ FakeAudioFileWriter へ書き込みを渡す。EngineRecorder は書き出し先を所有するので、
/// テストからも中身を見られるよう、本体は共有にしておく。
class SharedWriter final : public ac::AudioFileWriter {
public:
    explicit SharedWriter(std::shared_ptr<ac::FakeAudioFileWriter> writer)
        : writer_(std::move(writer)) {}
    common::Result<void, ac::AudioFileError> write(std::span<const float> interleaved) override {
        return writer_->write(interleaved);
    }
    common::Result<void, ac::AudioFileError> finish() override {
        return writer_->finish();
    }

private:
    std::shared_ptr<ac::FakeAudioFileWriter> writer_;
};

class EngineRecorderTest : public ::testing::Test {
protected:
    EngineRecorderTest()
        : mixer_(ac::MixerConfig{kMixerFaderCapacity, kMixerMeterCapacity}),
          engine_(mixer_, logger_),
          disk_(1ms),
          recorder_(EngineRecorder::Ports{
              engine_, disk_,
              [this](const std::filesystem::path& file, ac::AudioFileFormat format,
                     std::size_t channels)
                  -> common::Result<std::unique_ptr<ac::AudioFileWriter>, ac::AudioFileError> {
                  opened_.push_back(Opened{file, format, channels});
                  if (fail_open_at_ && *fail_open_at_ == opened_.size() - 1) {
                      return common::make_unexpected(ac::AudioFileError::kDiskFull);
                  }
                  auto writer = std::make_shared<ac::FakeAudioFileWriter>(channels);
                  if (configure_) {
                      configure_(*writer);
                  }
                  writers_.push_back(writer);
                  return std::unique_ptr<ac::AudioFileWriter>(
                      std::make_unique<SharedWriter>(writer));
              },
              [this](const std::filesystem::path& folder)
                  -> common::Result<std::filesystem::path, cp::RecorderResult> {
                  take_requests_.push_back(folder);
                  return temp_.path() / "take";
              },
              clock_, logger_}) {}

    struct Opened {
        std::filesystem::path file;
        ac::AudioFileFormat format;
        std::size_t channels;
    };

    // 1 回のコールバックぶんを描画する（テストのスレッドがオーディオスレッドの代わり）。
    void render(std::size_t callbacks = 1) {
        std::vector<float> out(kCallbackFrames * 2, 0.0F);
        for (std::size_t i = 0; i < callbacks; ++i) {
            mixer_.render(ac::InterleavedBuffer{out, 2});
            engine_.collect();
        }
    }

    void add_audible_strip(const std::string& id) {
        engine_.add_channel(id);
        ASSERT_TRUE(engine_.attach_source(
            id, std::make_shared<ac::FakeAudioSource>(ac::FakeAudioSource::constant(kInput))));
        engine_.set_channel_muted(id, false);
        engine_.set_master_muted(false);
    }

    static session::RecordingPlan plan(std::vector<std::string> sources) {
        session::RecordingPlan p;
        p.folder = "C:/Music/VOCROZ";
        p.format = ac::AudioFileFormat::kWav;
        int n = 1;
        for (auto& id : sources) {
            p.tracks.push_back(session::RecordingTrackPlan{id, "0" + std::to_string(n++) + ".wav"});
        }
        return p;
    }

    // ディスク I/O スレッドの周期を回して、録音を閉じ終えるまで進める。
    void finish_recording() {
        recorder_.stop();
        render();
        for (int i = 0; i < 3; ++i) {
            recorder_.collect();
            disk_.run_once();
        }
        recorder_.collect();
    }

    common::FakeLogger logger_;
    common::FakeClock clock_;
    TempDirectory temp_;
    ac::Mixer mixer_;
    EngineMixer engine_;
    DiskIoWorker disk_;
    std::vector<Opened> opened_;
    std::vector<std::shared_ptr<ac::FakeAudioFileWriter>> writers_;
    std::vector<std::filesystem::path> take_requests_;
    std::optional<std::size_t> fail_open_at_;
    std::function<void(ac::FakeAudioFileWriter&)> configure_;
    EngineRecorder recorder_;
};

TEST_F(EngineRecorderTest, StartOpensFilesInTakeFolderWithSourceChannels) {
    add_audible_strip("ch-1");
    const auto take = recorder_.start(plan({"master", "ch-1"}));
    ASSERT_TRUE(take);
    EXPECT_EQ(take.value(), "take");
    ASSERT_EQ(take_requests_.size(), 1U);
    EXPECT_EQ(take_requests_[0], std::filesystem::path(u8"C:/Music/VOCROZ"));
    ASSERT_EQ(opened_.size(), 2U);
    EXPECT_EQ(opened_[0].file, temp_.path() / "take" / "01.wav");
    // Master はステレオ、ストリップは受信ストリームのチャンネル数（FakeAudioSource はモノラル）。
    EXPECT_EQ(opened_[0].channels, 2U);
    EXPECT_EQ(opened_[1].channels, 1U);
}

TEST_F(EngineRecorderTest, RecordsStripPreFaderAndMasterPostFader) {
    add_audible_strip("ch-1");
    ASSERT_TRUE(recorder_.start(plan({"master", "ch-1"})));
    // ストリップのミュートは Master の音を止めるが、プリフェーダーで録るストリップは止めない。
    engine_.set_channel_muted("ch-1", true);
    render(4);
    disk_.run_once();

    ASSERT_EQ(writers_.size(), 2U);
    const auto master = writers_[0]->samples();
    const auto strip = writers_[1]->samples();
    ASSERT_FALSE(master.empty());
    ASSERT_FALSE(strip.empty());
    EXPECT_FLOAT_EQ(strip.back(), kInput);
    EXPECT_FLOAT_EQ(master.back(), 0.0F);
}

TEST_F(EngineRecorderTest, AllTracksStartAndEndOnTheSameBlock) {
    add_audible_strip("ch-1");
    ASSERT_TRUE(recorder_.start(plan({"master", "ch-1"})));
    render(3);
    disk_.run_once();
    render(2);
    finish_recording();

    ASSERT_EQ(writers_.size(), 2U);
    EXPECT_TRUE(writers_[0]->finished());
    EXPECT_TRUE(writers_[1]->finished());
    EXPECT_EQ(writers_[0]->frames(), 5 * kCallbackFrames);
    EXPECT_EQ(writers_[1]->frames(), 5 * kCallbackFrames);
}

TEST_F(EngineRecorderTest, FailedOpenStartsNothing) {
    add_audible_strip("ch-1");
    std::filesystem::create_directories(temp_.path() / "take");
    fail_open_at_ = 1;
    const auto take = recorder_.start(plan({"master", "ch-1"}));
    ASSERT_FALSE(take);
    EXPECT_EQ(take.error(), cp::RecorderResult::kDiskFull);
    // 開いたファイルは閉じ、テイクのフォルダも消す。
    ASSERT_EQ(writers_.size(), 1U);
    EXPECT_TRUE(writers_[0]->finished());
    EXPECT_FALSE(std::filesystem::exists(temp_.path() / "take"));
    render(2);
    EXPECT_EQ(writers_[0]->frames(), 0U);
    EXPECT_TRUE(recorder_.status().dropped_frames.empty());
}

TEST_F(EngineRecorderTest, StalledDiskDoesNotBlockAudioThread) {
    add_audible_strip("ch-1");
    configure_ = [](ac::FakeAudioFileWriter& writer) { writer.stall(); };
    ASSERT_TRUE(recorder_.start(plan({"ch-1"})));
    disk_.start();
    // ディスク I/O スレッドが書き込みの中で止まるまで待つ。
    for (int i = 0; i < 1000 && !writers_[0]->is_blocked(); ++i) {
        render();
        std::this_thread::sleep_for(1ms);
    }
    ASSERT_TRUE(writers_[0]->is_blocked());

    // リングバッファ（4 秒）を超える 5 秒ぶんを描画する。書き込みを待てば返らないので、
    // 返ってくること自体が「オーディオスレッドを止めない」ことの確認になる。
    const auto began = std::chrono::steady_clock::now();
    const std::size_t callbacks = 5 * ac::kSampleRate / kCallbackFrames;
    render(callbacks);
    EXPECT_LT(std::chrono::steady_clock::now() - began, 5s);
    // 溢れたぶんは欠落として数える。
    EXPECT_GT(recorder_.status().dropped_frames.at(0), 0U);

    writers_[0]->release();
    recorder_.stop();
    render();
    for (int i = 0; i < 2000; ++i) {
        recorder_.collect();
        if (writers_[0]->finished()) {
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    disk_.stop();
    ASSERT_TRUE(writers_[0]->finished());
    // 欠落したぶんは無音で埋めるので、ファイルの長さは描画した長さとそろう。
    const std::size_t rendered = writers_[0]->frames();
    EXPECT_GE(rendered, callbacks * kCallbackFrames);
}

TEST_F(EngineRecorderTest, DiskFullIsReportedWithoutStoppingLiveAudio) {
    add_audible_strip("ch-1");
    configure_ = [](ac::FakeAudioFileWriter& writer) { writer.set_capacity_frames(1000); };
    ASSERT_TRUE(recorder_.start(plan({"master"})));
    render(2);
    disk_.run_once();
    const auto status = recorder_.status();
    ASSERT_TRUE(status.failure);
    EXPECT_EQ(*status.failure, ac::AudioFileError::kDiskFull);

    // メインの出力は鳴り続ける。
    std::vector<float> out(kCallbackFrames * 2, 0.0F);
    mixer_.render(ac::InterleavedBuffer{out, 2});
    EXPECT_GT(out[out.size() - 2], 0.0F);
    finish_recording();
    // 書けなかったときも、書けたところまでで閉じる。
    EXPECT_TRUE(writers_[0]->finished());
}

TEST_F(EngineRecorderTest, ClosesOnlyAfterGraphSettles) {
    add_audible_strip("ch-1");
    ASSERT_TRUE(recorder_.start(plan({"master"})));
    render();
    recorder_.stop();
    // タップを外したグラフをオーディオスレッドがまだ使い始めていない。
    recorder_.collect();
    disk_.run_once();
    EXPECT_FALSE(writers_[0]->finished());

    render();
    recorder_.collect();
    disk_.run_once();
    EXPECT_TRUE(writers_[0]->finished());
}

TEST_F(EngineRecorderTest, ClosesAfterTimeoutWhenAudioIsNotRunning) {
    ASSERT_TRUE(recorder_.start(plan({"master"})));
    recorder_.stop();
    recorder_.collect();
    disk_.run_once();
    EXPECT_FALSE(writers_[0]->finished());

    // 出力デバイスを開いていなければ、グラフはいつまでも落ち着かない。
    clock_.advance(kRecordingFinishTimeout);
    recorder_.collect();
    disk_.run_once();
    EXPECT_TRUE(writers_[0]->finished());
}

TEST_F(EngineRecorderTest, StatusIsEmptyWhenNotRecording) {
    const auto status = recorder_.status();
    EXPECT_TRUE(status.dropped_frames.empty());
    EXPECT_FALSE(status.failure);
}

TEST(CreateTakeFolderTest, NamesByLocalTimeAndAvoidsExistingFolders) {
    TempDirectory temp;
    const auto media = temp.path() / "media";
    std::tm tm{};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 10;
    tm.tm_mday = 15;
    tm.tm_hour = 20;
    tm.tm_min = 15;
    tm.tm_sec = 3;
    tm.tm_isdst = -1;
    const auto when = std::chrono::system_clock::from_time_t(std::mktime(&tm));

    const auto first = create_take_folder(media, when);
    ASSERT_TRUE(first);
    EXPECT_EQ(first.value().filename(), "20261115-201503");
    EXPECT_TRUE(std::filesystem::is_directory(first.value()));

    const auto second = create_take_folder(media, when);
    ASSERT_TRUE(second);
    EXPECT_EQ(second.value().filename(), "20261115-201503-2");
}

TEST(CreateTakeFolderTest, FailsWhenMediaFolderCannotBeCreated) {
    TempDirectory temp;
    // ファイルがある場所にはフォルダを作れない。
    const auto blocked = temp.path() / "file";
    { std::ofstream(blocked) << "x"; }
    const auto result = create_take_folder(blocked / "media", std::chrono::system_clock::now());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), cp::RecorderResult::kFolderUnavailable);
}

}  // namespace
}  // namespace vocroz::engine
