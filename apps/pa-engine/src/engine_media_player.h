// SPDX-License-Identifier: AGPL-3.0-only
//
// session の MediaPlayerControl の実装（docs/adr/0043-recording-and-media-player.md）。
// ファイルを開き、再生トラック（audio-core の PlaybackTrack）の再生ノードを流す先へつなぎ、
// 先読み（ファイルの読み込みとデコード）をディスク I/O スレッドに任せる。読み込みが遅れても
// オーディオスレッドは待たず、間に合わないぶんを無音で埋める。
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "vocroz/audio_core/audio_file_error.h"
#include "vocroz/audio_core/playback_track.h"
#include "vocroz/audio_core/ports/audio_file_reader.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"
#include "vocroz/session/ports/media_player_control.h"

#include "disk_io_worker.h"
#include "engine_mixer.h"

namespace vocroz::engine {

/// 再生ファイル 1 つの先読み。ディスク I/O スレッドで回す。
/// スレッド安全性: service() はディスク I/O スレッドだけが呼ぶ。failure() はどのスレッドからも
/// 呼んでよい。
class PlaybackJob final : public DiskJob {
public:
    PlaybackJob(std::shared_ptr<audio_core::PlaybackTrack> track,
                std::unique_ptr<audio_core::AudioFileReader> reader);

    /// 先読みに空きがあるぶんだけ読む。一度失敗したら読まない（先読み済みのぶんは鳴り続ける）。
    void service() override;

    /// 読み込みが失敗していれば、その理由。
    [[nodiscard]] std::optional<audio_core::AudioFileError> failure() const noexcept;

private:
    // 失敗の理由。kNoFailure なら失敗していない。
    static constexpr int kNoFailure = -1;

    std::shared_ptr<audio_core::PlaybackTrack> track_;
    std::unique_ptr<audio_core::AudioFileReader> reader_;
    std::atomic<int> failure_{kNoFailure};
};

/// メディアプレイヤー。
/// スレッド安全性: すべてのメンバー関数をコントロールスレッドから呼ぶ。
class EngineMediaPlayer final : public session::MediaPlayerControl {
public:
    /// ファイルを開く関数（media の make_audio_file_reader()）。
    using ReaderFactory = std::function<
        common::Result<std::unique_ptr<audio_core::AudioFileReader>, audio_core::AudioFileError>(
            const std::filesystem::path& file)>;

    /// 注入する依存。このオブジェクトより長く生存させる。
    struct Ports {
        EngineMixer& mixer;
        DiskIoWorker& disk;
        ReaderFactory open_reader;
        common::Logger& logger;
    };

    explicit EngineMediaPlayer(Ports ports);
    EngineMediaPlayer(const EngineMediaPlayer&) = delete;
    EngineMediaPlayer& operator=(const EngineMediaPlayer&) = delete;
    ~EngineMediaPlayer() override;

    // --- session::MediaPlayerControl ---
    [[nodiscard]] common::Result<session::MediaInfo, control_protocol::MediaResult> load(
        const std::string& file, const std::string& target) override;
    void unload() override;
    void play() override;
    void pause() override;
    void seek(std::uint64_t frame) override;
    void set_target(const std::string& target) override;
    [[nodiscard]] session::MediaPlayerStatus status() const override;

private:
    Ports ports_;
    std::shared_ptr<audio_core::PlaybackTrack> track_;
    std::shared_ptr<PlaybackJob> job_;
    std::uint64_t length_frames_ = 0;
};

}  // namespace vocroz::engine
