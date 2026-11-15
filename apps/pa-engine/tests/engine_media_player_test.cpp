// SPDX-License-Identifier: AGPL-3.0-only
//
// EngineMediaPlayer の単体テスト。本物の Mixer と EngineMixer に再生ノードをつなぎ、テストの
// スレッドをオーディオスレッドに見立てて render() を回す。読み込んだ直後は鳴らないこと
// （初期ミュート）、先読みが遅れても render() は待たずに無音で埋めること、読み込みの失敗を
// status() で返すことを確かめる（AGENTS.md 第 9 節）。

#include "engine_media_player.h"

#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/audio_core/mixer.h"

#include "engine_constants.h"
#include "fakes/fake_audio_file_reader.h"
#include "fakes/fake_logger.h"

namespace vocroz::engine {
namespace {

namespace ac = audio_core;
namespace cp = control_protocol;
using namespace std::chrono_literals;

constexpr std::size_t kCallbackFrames = 1024;
constexpr std::uint64_t kFileFrames = 48'000;

/// テストが持つ FakeAudioFileReader へ読み込みを渡す。
class SharedReader final : public ac::AudioFileReader {
public:
    explicit SharedReader(std::shared_ptr<ac::FakeAudioFileReader> reader)
        : reader_(std::move(reader)) {}
    [[nodiscard]] std::size_t channel_count() const noexcept override {
        return reader_->channel_count();
    }
    [[nodiscard]] std::uint64_t length_frames() const noexcept override {
        return reader_->length_frames();
    }
    common::Result<std::size_t, ac::AudioFileError> read(std::span<float> interleaved) override {
        return reader_->read(interleaved);
    }
    common::Result<void, ac::AudioFileError> seek(std::uint64_t frame) override {
        return reader_->seek(frame);
    }

private:
    std::shared_ptr<ac::FakeAudioFileReader> reader_;
};

class EngineMediaPlayerTest : public ::testing::Test {
protected:
    EngineMediaPlayerTest()
        : mixer_(ac::MixerConfig{kMixerFaderCapacity, kMixerMeterCapacity}),
          engine_(mixer_, logger_),
          disk_(1ms),
          player_(EngineMediaPlayer::Ports{
              engine_, disk_,
              [this](const std::filesystem::path& file)
                  -> common::Result<std::unique_ptr<ac::AudioFileReader>, ac::AudioFileError> {
                  opened_.push_back(file);
                  if (open_error_) {
                      return common::make_unexpected(*open_error_);
                  }
                  reader_ = std::make_shared<ac::FakeAudioFileReader>(2, kFileFrames);
                  return std::unique_ptr<ac::AudioFileReader>(
                      std::make_unique<SharedReader>(reader_));
              },
              logger_}) {
        engine_.set_master_muted(false);
    }

    // 1 回のコールバックぶんを描画し、出力の最大の振幅を返す。
    float render(std::size_t callbacks = 1) {
        std::vector<float> out(kCallbackFrames * 2, 0.0F);
        float peak = 0.0F;
        for (std::size_t i = 0; i < callbacks; ++i) {
            std::fill(out.begin(), out.end(), 0.0F);
            mixer_.render(ac::InterleavedBuffer{out, 2});
            engine_.collect();
            for (const float s : out) {
                peak = std::max(peak, std::abs(s));
            }
        }
        return peak;
    }

    common::FakeLogger logger_;
    ac::Mixer mixer_;
    EngineMixer engine_;
    DiskIoWorker disk_;
    std::vector<std::filesystem::path> opened_;
    std::shared_ptr<ac::FakeAudioFileReader> reader_;
    std::optional<ac::AudioFileError> open_error_;
    EngineMediaPlayer player_;
};

TEST_F(EngineMediaPlayerTest, LoadReportsFileInfoAndStaysSilent) {
    const auto info = player_.load("C:/Music/VOCROZ/a.wav", "master");
    ASSERT_TRUE(info);
    EXPECT_EQ(info.value().channels, 2U);
    EXPECT_EQ(info.value().length_frames, kFileFrames);
    ASSERT_EQ(opened_.size(), 1U);
    disk_.run_once();
    // 読み込んだだけでは鳴らさない（初期ミュート）。
    EXPECT_FLOAT_EQ(render(2), 0.0F);
    EXPECT_EQ(player_.status().position_frames, 0U);
}

TEST_F(EngineMediaPlayerTest, PlaysToTargetAfterPlay) {
    ASSERT_TRUE(player_.load("a.wav", "master"));
    disk_.run_once();
    player_.play();
    EXPECT_GT(render(4), 0.0F);
    EXPECT_GT(player_.status().position_frames, 0U);
}

TEST_F(EngineMediaPlayerTest, StalledReadDoesNotBlockAudioThread) {
    ASSERT_TRUE(player_.load("a.wav", "master"));
    // 先読みを 1 度も回さないまま再生しても、render() は待たずに返る。
    player_.play();
    const auto started = std::chrono::steady_clock::now();
    EXPECT_FLOAT_EQ(render(8), 0.0F);
    EXPECT_LT(std::chrono::steady_clock::now() - started, 1s);
}

TEST_F(EngineMediaPlayerTest, ReadFailureIsReported) {
    ASSERT_TRUE(player_.load("a.wav", "master"));
    reader_->fail_next_read(ac::AudioFileError::kIoError);
    disk_.run_once();
    ASSERT_TRUE(player_.status().failure);
    EXPECT_EQ(*player_.status().failure, ac::AudioFileError::kIoError);
    // 失敗した後もオーディオスレッドは止まらない。
    player_.play();
    (void)render(2);
}

TEST_F(EngineMediaPlayerTest, OpenFailureMapsToMediaResult) {
    open_error_ = ac::AudioFileError::kUnsupportedFormat;
    const auto info = player_.load("a.txt", "master");
    ASSERT_FALSE(info);
    EXPECT_EQ(info.error(), cp::MediaResult::kUnsupportedFormat);
    open_error_ = ac::AudioFileError::kNotFound;
    EXPECT_EQ(player_.load("a.wav", "master").error(), cp::MediaResult::kNotFound);
}

TEST_F(EngineMediaPlayerTest, SeekIsClampedToLength) {
    ASSERT_TRUE(player_.load("a.wav", "master"));
    player_.seek(kFileFrames * 2);
    disk_.run_once();
    EXPECT_LE(player_.status().position_frames, kFileFrames);
}

TEST_F(EngineMediaPlayerTest, UnloadSilencesAndClearsStatus) {
    ASSERT_TRUE(player_.load("a.wav", "master"));
    disk_.run_once();
    player_.play();
    (void)render(2);
    player_.unload();
    (void)render(2);
    EXPECT_FLOAT_EQ(render(2), 0.0F);
    EXPECT_EQ(player_.status().position_frames, 0U);
    EXPECT_FALSE(player_.status().failure);
}

TEST_F(EngineMediaPlayerTest, UnknownTargetPausesOnRetarget) {
    ASSERT_TRUE(player_.load("a.wav", "master"));
    disk_.run_once();
    player_.play();
    // 空の流す先にはつなげないので、前の流す先で鳴らし続けずに止める。
    EXPECT_GT(render(2), 0.0F);
    player_.set_target("");
    (void)render(2);
    EXPECT_FLOAT_EQ(render(2), 0.0F);
}

}  // namespace
}  // namespace vocroz::engine
