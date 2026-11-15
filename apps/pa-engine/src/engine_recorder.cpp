// SPDX-License-Identifier: AGPL-3.0-only
//
// engine_recorder.h の実装。

#include "engine_recorder.h"

#include <ctime>
#include <string>
#include <system_error>
#include <utility>

#include "engine_constants.h"
#include "utf8_path.h"

namespace vocroz::engine {
namespace {

namespace ac = audio_core;
namespace cp = control_protocol;

/// 同じ秒に始めたテイクの名前に付ける番号の上限。1 秒に何度も始め直すことはないので、
/// これを超えたら作れないとみなす。
constexpr int kMaxTakeSuffix = 99;

cp::RecorderResult to_recorder_result(ac::AudioFileError error) noexcept {
    switch (error) {
        case ac::AudioFileError::kDiskFull:
            return cp::RecorderResult::kDiskFull;
        case ac::AudioFileError::kIoError:
        case ac::AudioFileError::kNotFound:
        case ac::AudioFileError::kUnsupportedFormat:
            return cp::RecorderResult::kIoError;
    }
    return cp::RecorderResult::kIoError;
}

std::tm local_time(std::chrono::system_clock::time_point now) {
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

}  // namespace

// --- RecordingJob ---

RecordingJob::RecordingJob(std::vector<Track> tracks) : tracks_(std::move(tracks)) {}

void RecordingJob::service() {
    if (finished_.load(std::memory_order_acquire)) {
        return;
    }
    if (finish_requested_.load(std::memory_order_acquire)) {
        for (auto& t : tracks_) {
            // 失敗しても、書けたところまでは閉じて残す。失敗は track の failure() に残る。
            (void)t.track->finish(*t.writer);
        }
        finished_.store(true, std::memory_order_release);
        return;
    }
    for (auto& t : tracks_) {
        // 1 本が失敗しても、ほかのトラックは書き続ける。全体を止めるかは session が決める
        // （failure() を見て次の周期で止める）。
        (void)t.track->drain(*t.writer);
    }
}

void RecordingJob::request_finish() noexcept {
    finish_requested_.store(true, std::memory_order_release);
}

bool RecordingJob::finished() const noexcept {
    return finished_.load(std::memory_order_acquire);
}

// --- EngineRecorder ---

EngineRecorder::EngineRecorder(Ports ports) : ports_(std::move(ports)) {}

EngineRecorder::~EngineRecorder() {
    stop();
    // コンポジションルートは、出力デバイスを閉じてからこのオブジェクトを破棄する。録音タップに
    // 書くスレッドはもうないので、落ち着くのを待たずに閉じるよう頼む。閉じるのは、ディスク I/O
    // スレッドが止まる前の最後の周期。
    for (auto& s : stopping_) {
        s.job->request_finish();
    }
}

common::Result<std::string, cp::RecorderResult> EngineRecorder::start(
    const session::RecordingPlan& plan) {
    if (active_ || plan.tracks.empty()) {
        return common::make_unexpected(cp::RecorderResult::kIoError);
    }
    auto take = ports_.make_take_folder(utf8_path(plan.folder));
    if (!take) {
        ports_.logger.log(common::LogLevel::kWarning, "録音のテイクのフォルダを作れませんでした");
        return common::make_unexpected(take.error());
    }
    const std::filesystem::path& take_folder = take.value();

    std::vector<RecordingJob::Track> tracks;
    std::vector<std::filesystem::path> files;
    std::vector<RecordTapAssignment> taps;
    for (const auto& planned : plan.tracks) {
        // 録り始めたときのチャンネル数で録る。録音中に子機の入力が増えても、ファイルの形式は
        // 変えない（つなげなくなった対象は無音になる）。
        const std::size_t channels =
            ports_.mixer.recording_channels(planned.source_id).value_or(kBusRecordingChannels);
        const auto file = take_folder / utf8_path(planned.file_name);
        auto writer = ports_.open_writer(file, plan.format, channels);
        if (!writer) {
            ports_.logger.log(common::LogLevel::kWarning, "録音のファイルを開けませんでした");
            discard(tracks, files, take_folder);
            return common::make_unexpected(to_recorder_result(writer.error()));
        }
        files.push_back(file);
        auto track = std::make_shared<ac::RecordingTrack>(channels);
        taps.push_back(RecordTapAssignment{planned.source_id, track});
        tracks.push_back(RecordingJob::Track{std::move(track), std::move(writer).value()});
    }

    auto job = std::make_shared<RecordingJob>(std::move(tracks));
    // 書き出しを先に回し始めてから、録音タップを足す。タップが書き始めたときに、リングバッファを
    // 空けるスレッドが既に回っているようにするため。
    ports_.disk.add(job);
    if (!ports_.mixer.attach_record_taps(std::move(taps))) {
        ports_.disk.remove(job);
        // まだ録音タップはグラフに入っていないので、すぐに閉じてよい。
        job->request_finish();
        job->service();
        discard_files(files, take_folder);
        return common::make_unexpected(cp::RecorderResult::kIoError);
    }
    active_ = std::move(job);
    return path_utf8(take_folder.filename());
}

void EngineRecorder::stop() {
    if (!active_) {
        return;
    }
    // すべての録音タップを 1 つのグラフで外し、どのトラックも同じブロックで録り終える。
    ports_.mixer.detach_record_taps();
    stopping_.push_back(Stopping{std::move(active_), ports_.clock.now()});
    active_.reset();
}

session::RecordingStatus EngineRecorder::status() const {
    session::RecordingStatus status;
    if (!active_) {
        return status;
    }
    for (const auto& t : active_->tracks()) {
        status.dropped_frames.push_back(t.track->dropped_frames());
        if (!status.failure) {
            status.failure = t.track->failure();
        }
    }
    return status;
}

void EngineRecorder::collect() {
    if (stopping_.empty()) {
        return;
    }
    const bool settled = ports_.mixer.is_graph_settled();
    const auto now = ports_.clock.now();
    for (auto& s : stopping_) {
        if (settled || now - s.since >= kRecordingFinishTimeout) {
            s.job->request_finish();
        }
    }
    std::erase_if(stopping_, [this](const Stopping& s) {
        if (!s.job->finished()) {
            return false;
        }
        ports_.disk.remove(s.job);
        return true;
    });
}

void EngineRecorder::discard(std::vector<RecordingJob::Track>& tracks,
                             const std::vector<std::filesystem::path>& files,
                             const std::filesystem::path& take) {
    for (auto& t : tracks) {
        (void)t.writer->finish();
    }
    tracks.clear();
    discard_files(files, take);
}

void EngineRecorder::discard_files(const std::vector<std::filesystem::path>& files,
                                   const std::filesystem::path& take) {
    // 何も始めなかったことにする。中途半端なテイクを残すと、ライブラリに空のファイルが並ぶため。
    std::error_code ignored;
    for (const auto& file : files) {
        std::filesystem::remove(file, ignored);
    }
    std::filesystem::remove(take, ignored);
}

common::Result<std::filesystem::path, cp::RecorderResult> create_take_folder(
    const std::filesystem::path& media_folder, std::chrono::system_clock::time_point now) {
    std::error_code error;
    std::filesystem::create_directories(media_folder, error);
    if (error || !std::filesystem::is_directory(media_folder, error)) {
        return common::make_unexpected(cp::RecorderResult::kFolderUnavailable);
    }
    const std::tm tm = local_time(now);
    char base[32];
    std::strftime(base, sizeof(base), "%Y%m%d-%H%M%S", &tm);
    for (int suffix = 1; suffix <= kMaxTakeSuffix; ++suffix) {
        const std::string name =
            suffix == 1 ? std::string(base) : std::string(base) + "-" + std::to_string(suffix);
        const auto folder = media_folder / name;
        // create_directory() は、既にあれば false を返す。別の録音のフォルダを使い回さない。
        if (std::filesystem::create_directory(folder, error)) {
            return folder;
        }
        if (error) {
            return common::make_unexpected(cp::RecorderResult::kFolderUnavailable);
        }
    }
    return common::make_unexpected(cp::RecorderResult::kFolderUnavailable);
}

}  // namespace vocroz::engine
