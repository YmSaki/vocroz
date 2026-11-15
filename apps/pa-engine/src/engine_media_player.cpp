// SPDX-License-Identifier: AGPL-3.0-only
//
// engine_media_player.h の実装。

#include "engine_media_player.h"

#include <algorithm>
#include <utility>

#include "utf8_path.h"

namespace vocroz::engine {
namespace {

namespace ac = audio_core;
namespace cp = control_protocol;

cp::MediaResult to_media_result(ac::AudioFileError error) noexcept {
    switch (error) {
        case ac::AudioFileError::kNotFound:
            return cp::MediaResult::kNotFound;
        case ac::AudioFileError::kUnsupportedFormat:
            return cp::MediaResult::kUnsupportedFormat;
        case ac::AudioFileError::kDiskFull:
        case ac::AudioFileError::kIoError:
            return cp::MediaResult::kIoError;
    }
    return cp::MediaResult::kIoError;
}

}  // namespace

// --- PlaybackJob ---

PlaybackJob::PlaybackJob(std::shared_ptr<ac::PlaybackTrack> track,
                         std::unique_ptr<ac::AudioFileReader> reader)
    : track_(std::move(track)), reader_(std::move(reader)) {}

void PlaybackJob::service() {
    if (failure_.load(std::memory_order_acquire) != kNoFailure) {
        return;
    }
    if (auto filled = track_->fill(*reader_); !filled) {
        failure_.store(static_cast<int>(filled.error()), std::memory_order_release);
    }
}

std::optional<ac::AudioFileError> PlaybackJob::failure() const noexcept {
    const int value = failure_.load(std::memory_order_acquire);
    if (value == kNoFailure) {
        return std::nullopt;
    }
    return static_cast<ac::AudioFileError>(value);
}

// --- EngineMediaPlayer ---

EngineMediaPlayer::EngineMediaPlayer(Ports ports) : ports_(std::move(ports)) {}

EngineMediaPlayer::~EngineMediaPlayer() {
    unload();
}

common::Result<session::MediaInfo, cp::MediaResult> EngineMediaPlayer::load(
    const std::string& file, const std::string& target) {
    unload();
    auto reader = ports_.open_reader(utf8_path(file));
    if (!reader) {
        ports_.logger.log(common::LogLevel::kWarning, "再生するファイルを開けませんでした");
        return common::make_unexpected(to_media_result(reader.error()));
    }
    const std::size_t channels = reader.value()->channel_count();
    const std::uint64_t length = reader.value()->length_frames();
    // 作った直後の再生トラックは止まっていて、play() まで鳴らない（初期ミュート）。
    auto track = std::make_shared<ac::PlaybackTrack>(channels);
    auto job = std::make_shared<PlaybackJob>(track, std::move(reader).value());
    // 先読みを先に始め、再生を押したときにすぐ鳴るようにする。
    ports_.disk.add(job);
    if (!ports_.mixer.attach_player(PlayerAssignment{target, track})) {
        ports_.disk.remove(job);
        return common::make_unexpected(cp::MediaResult::kIoError);
    }
    track_ = std::move(track);
    job_ = std::move(job);
    length_frames_ = length;
    return session::MediaInfo{channels, length};
}

void EngineMediaPlayer::unload() {
    if (!track_) {
        return;
    }
    track_->pause();
    // トラックの最後の参照は古いグラフの破棄で、読み込み元はディスク I/O スレッドの周期の後で
    // 外れる。どちらもオーディオスレッドの外。
    ports_.mixer.detach_player();
    ports_.disk.remove(job_);
    job_.reset();
    track_.reset();
    length_frames_ = 0;
}

void EngineMediaPlayer::play() {
    if (track_) {
        track_->play();
    }
}

void EngineMediaPlayer::pause() {
    if (track_) {
        track_->pause();
    }
}

void EngineMediaPlayer::seek(std::uint64_t frame) {
    if (track_) {
        track_->seek(std::min(frame, length_frames_));
    }
}

void EngineMediaPlayer::set_target(const std::string& target) {
    if (!track_) {
        return;
    }
    if (!ports_.mixer.attach_player(PlayerAssignment{target, track_})) {
        // 付け替えられなければ鳴らさない（前の流す先に鳴らし続けない）。
        track_->pause();
    }
}

session::MediaPlayerStatus EngineMediaPlayer::status() const {
    session::MediaPlayerStatus status;
    if (!track_) {
        return status;
    }
    status.position_frames = track_->position_frames();
    status.at_end = track_->at_end();
    status.failure = job_->failure();
    return status;
}

}  // namespace vocroz::engine
