// SPDX-License-Identifier: AGPL-3.0-only
//
// session の RecorderControl の実装（docs/adr/0043-recording-and-media-player.md）。
// テイクのフォルダとトラックのファイルを開き、録音トラック（audio-core の RecordingTrack）の
// 録音タップを 1 つのグラフでミキサーに足し、書き出しをディスク I/O スレッドに任せる。
//
// 止め方: 録音タップを外したグラフを適用し、オーディオスレッドが古いグラフを使い終えたら
// （EngineMixer::is_graph_settled()）、ディスク I/O スレッドが残りを書いてファイルを閉じる。
// 待つのは collect() の周期の中で、stop() は待たない。出力デバイスを開いていないとオーディオ
// スレッドが回らず、グラフはいつまでも落ち着かないので、kRecordingFinishTimeout を過ぎたら
// 閉じる（そのときは録音タップに書くスレッドがない）。
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include "vocroz/audio_core/audio_file_error.h"
#include "vocroz/audio_core/audio_file_format.h"
#include "vocroz/audio_core/ports/audio_file_writer.h"
#include "vocroz/audio_core/recording_track.h"
#include "vocroz/common/ports/clock.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"
#include "vocroz/session/ports/recorder_control.h"

#include "disk_io_worker.h"
#include "engine_mixer.h"

namespace vocroz::engine {

/// 録音 1 回ぶんの書き出し。ディスク I/O スレッドで回す。
/// スレッド安全性: service() はディスク I/O スレッドだけが呼ぶ。request_finish() と取得関数は
/// コントロールスレッドから呼んでよい。
class RecordingJob final : public DiskJob {
public:
    /// トラック 1 本と、その書き出し先。
    struct Track {
        std::shared_ptr<audio_core::RecordingTrack> track;
        std::unique_ptr<audio_core::AudioFileWriter> writer;
    };

    explicit RecordingJob(std::vector<Track> tracks);

    /// 書き出す。終わりを頼まれていれば、残りを書いてファイルを閉じる。
    void service() override;

    /// 録音タップを含むグラフがすべて破棄された後に呼ぶ。次の周期でファイルを閉じる。
    void request_finish() noexcept;

    /// ファイルを閉じ終えたら true。
    [[nodiscard]] bool finished() const noexcept;

    /// トラック（開始した順）。
    [[nodiscard]] const std::vector<Track>& tracks() const noexcept {
        return tracks_;
    }

private:
    std::vector<Track> tracks_;
    std::atomic<bool> finish_requested_{false};
    std::atomic<bool> finished_{false};
};

/// マルチトラック録音。
/// スレッド安全性: すべてのメンバー関数をコントロールスレッドから呼ぶ。
class EngineRecorder final : public session::RecorderControl {
public:
    /// トラックのファイルを開く関数（media の make_audio_file_writer()）。
    using WriterFactory = std::function<
        common::Result<std::unique_ptr<audio_core::AudioFileWriter>, audio_core::AudioFileError>(
            const std::filesystem::path& file, audio_core::AudioFileFormat format,
            std::size_t channels)>;
    /// メディアフォルダの中にテイクのフォルダを作る関数（create_take_folder()）。
    using TakeFolderFactory =
        std::function<common::Result<std::filesystem::path, control_protocol::RecorderResult>(
            const std::filesystem::path& media_folder)>;

    /// 注入する依存。このオブジェクトより長く生存させる。
    struct Ports {
        EngineMixer& mixer;
        DiskIoWorker& disk;
        WriterFactory open_writer;
        TakeFolderFactory make_take_folder;
        const common::Clock& clock;
        common::Logger& logger;
    };

    explicit EngineRecorder(Ports ports);
    EngineRecorder(const EngineRecorder&) = delete;
    EngineRecorder& operator=(const EngineRecorder&) = delete;
    /// 録音中なら録音タップを外し、ファイルを閉じるよう頼む（閉じるのはディスク I/O スレッドが
    /// 止まる前の最後の周期）。
    ~EngineRecorder() override;

    // --- session::RecorderControl ---
    [[nodiscard]] common::Result<std::string, control_protocol::RecorderResult> start(
        const session::RecordingPlan& plan) override;
    void stop() override;
    [[nodiscard]] session::RecordingStatus status() const override;

    /// 周期処理。止めた録音のグラフが落ち着いていたらファイルを閉じるよう頼み、閉じ終えた録音を
    /// ディスク I/O スレッドから外す。
    void collect();

private:
    // 開いたファイルを閉じて消し、テイクのフォルダも消す（何も始めなかったことにする）。
    void discard(std::vector<RecordingJob::Track>& tracks,
                 const std::vector<std::filesystem::path>& files,
                 const std::filesystem::path& take);
    // 開いたファイルとテイクのフォルダを消す。
    static void discard_files(const std::vector<std::filesystem::path>& files,
                              const std::filesystem::path& take);

    // 止めて、ファイルを閉じるのを待っている録音。
    struct Stopping {
        std::shared_ptr<RecordingJob> job;
        common::Clock::TimePoint since;
    };

    Ports ports_;
    std::shared_ptr<RecordingJob> active_;
    std::vector<Stopping> stopping_;
};

/// メディアフォルダの中に、開始した時刻（ローカル時刻）の YYYYMMDD-HHMMSS のテイクのフォルダを
/// 作る。同じ名前があれば -2、-3 を付ける。メディアフォルダがなければ作る。
/// @param media_folder メディアフォルダ
/// @param now          開始した時刻
/// @return 作ったフォルダ。作れなければ FOLDER_UNAVAILABLE
/// スレッド安全性: どのスレッドからも呼び出し可能。ファイルシステムを操作する。
[[nodiscard]] common::Result<std::filesystem::path, control_protocol::RecorderResult>
create_take_folder(const std::filesystem::path& media_folder,
                   std::chrono::system_clock::time_point now);

}  // namespace vocroz::engine
