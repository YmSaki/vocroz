// SPDX-License-Identifier: AGPL-3.0-only
//
// 録音とメディアプレイヤーの状態の遷移（docs/adr/0043-recording-and-media-player.md）。
#include "media_desk.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <utility>

#include "vocroz/audio_core/constants.h"
#include "vocroz/control_protocol/constants.h"
#include "vocroz/session/entity_ids.h"

#include "detail/file_name_utils.h"
#include "detail/revision_utils.h"
#include "session_constants.h"

namespace vocroz::session {

namespace cp = control_protocol;
using cp::ErrorCode;

namespace {

/// 表示名がファイル名に使えないとき（記号だけの名前など）のトラックの名前。
constexpr const char* kFallbackTrackName = "Track";

/// 1 ms のフレーム数。
constexpr std::uint64_t kFramesPerMs = audio_core::kSampleRate / 1000;

std::uint64_t frames_to_ms(std::uint64_t frames) noexcept {
    return frames / kFramesPerMs;
}

audio_core::AudioFileFormat to_file_format(cp::RecordingFormat format) noexcept {
    switch (format) {
        case cp::RecordingFormat::kWav:
            return audio_core::AudioFileFormat::kWav;
        case cp::RecordingFormat::kFlac:
            return audio_core::AudioFileFormat::kFlac;
    }
    return audio_core::AudioFileFormat::kWav;
}

const char* extension_of(cp::RecordingFormat format) noexcept {
    switch (format) {
        case cp::RecordingFormat::kWav:
            return ".wav";
        case cp::RecordingFormat::kFlac:
            return ".flac";
    }
    return ".wav";
}

/// ファイル名の番号。設定の順に 01 から振り、エクスプローラーで名前順に並べたときに録る対象の
/// 順に並ぶようにする（トラックは最大 32 本なので 2 桁で足りる）。
std::string track_number(std::size_t index) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%02zu", index + 1);
    return buffer;
}

cp::RecorderResult to_recorder_result(audio_core::AudioFileError error) noexcept {
    switch (error) {
        case audio_core::AudioFileError::kDiskFull:
            return cp::RecorderResult::kDiskFull;
        case audio_core::AudioFileError::kIoError:
        case audio_core::AudioFileError::kNotFound:
        case audio_core::AudioFileError::kUnsupportedFormat:
            return cp::RecorderResult::kIoError;
    }
    return cp::RecorderResult::kIoError;
}

cp::MediaResult to_media_result(audio_core::AudioFileError error) noexcept {
    switch (error) {
        case audio_core::AudioFileError::kNotFound:
            return cp::MediaResult::kNotFound;
        case audio_core::AudioFileError::kUnsupportedFormat:
            return cp::MediaResult::kUnsupportedFormat;
        case audio_core::AudioFileError::kDiskFull:
        case audio_core::AudioFileError::kIoError:
            return cp::MediaResult::kIoError;
    }
    return cp::MediaResult::kIoError;
}

}  // namespace

MediaDesk::MediaDesk(Ports ports, std::string default_folder)
    : ports_(ports), default_folder_(std::move(default_folder)) {
    recorder_.entity_id = kRecorderEntityId;
    recorder_.config.folder = default_folder_;
    player_.entity_id = kMediaPlayerEntityId;
    player_.target = kMasterEntityId;
}

void MediaDesk::restore(const cp::RecorderConfig& saved, const SourceLabel& label_of) {
    recorder_.config = saved;
    if (recorder_.config.folder.empty()) {
        recorder_.config.folder = default_folder_;
    }
    std::erase_if(recorder_.config.sources,
                  [&](const std::string& id) { return !label_of(id).has_value(); });
    rebuild_library();
}

cp::RecorderConfig MediaDesk::saved_config(const TargetCheck& keep) const {
    cp::RecorderConfig config = recorder_.config;
    std::erase_if(config.sources, [&](const std::string& id) { return !keep(id); });
    // 既定のフォルダは保存しない。ユーザーの音楽フォルダが変わっても追従させるため。
    if (config.folder == default_folder_) {
        config.folder.clear();
    }
    return config;
}

MediaDesk::CommandResult MediaDesk::set_recorder_config(cp::RecorderConfig config,
                                                        const SourceLabel& label_of) {
    if (recorder_.state == cp::RecorderState::kRecording) {
        // 録音中にトラックやフォルダを変えると、録っているファイルと表示が食い違う。
        return common::make_unexpected(ErrorCode::kInvalidState);
    }
    if (config.sources.size() > cp::kMaxRecordingTracks) {
        return common::make_unexpected(ErrorCode::kNotFound);
    }
    std::set<std::string> seen;
    for (const auto& id : config.sources) {
        if (!label_of(id) || !seen.insert(id).second) {
            return common::make_unexpected(ErrorCode::kNotFound);
        }
    }
    if (config.folder.empty()) {
        config.folder = default_folder_;
    }
    if (config == recorder_.config) {
        return recorder_.revision;
    }
    const bool folder_changed = config.folder != recorder_.config.folder;
    recorder_.config = std::move(config);
    detail::bump_revision(recorder_.revision);
    publish_recorder();
    if (folder_changed) {
        rebuild_library();
        publish_library();
    }
    return recorder_.revision;
}

MediaDesk::CommandResult MediaDesk::set_recording(bool recording, const SourceLabel& label_of) {
    const bool now_recording = recorder_.state == cp::RecorderState::kRecording;
    if (recording == now_recording) {
        return recorder_.revision;
    }
    if (!recording) {
        stop_recording();
        detail::bump_revision(recorder_.revision);
        publish_recorder();
        // 録ったテイクをすぐにプレイヤーで選べるようにする。
        rebuild_library();
        publish_library();
        return recorder_.revision;
    }

    RecordingPlan plan;
    plan.folder = recorder_.config.folder;
    plan.format = to_file_format(recorder_.config.format);
    for (const auto& id : recorder_.config.sources) {
        // 設定した後で参加者が抜けたストリップは録らない（ファイルを作っても無音になるだけ）。
        const auto label = label_of(id);
        if (!label) {
            continue;
        }
        const std::string name =
            detail::to_file_name_part(*label, kMaxRecordingNameBytes, kFallbackTrackName);
        plan.tracks.push_back(RecordingTrackPlan{id, track_number(plan.tracks.size()) + "-" + name +
                                                         extension_of(recorder_.config.format)});
    }
    if (plan.tracks.empty()) {
        return common::make_unexpected(ErrorCode::kInvalidState);
    }

    auto started = ports_.recorder.start(plan);
    recorder_.elapsed_ms = 0;
    recorder_.tracks.clear();
    if (started) {
        recorder_.state = cp::RecorderState::kRecording;
        recorder_.take = std::move(started).value();
        for (auto& track : plan.tracks) {
            recorder_.tracks.push_back(
                cp::RecordingTrackInfo{std::move(track.source_id), std::move(track.file_name), 0});
        }
        recorder_.last_result = cp::RecorderResult::kOk;
        recording_since_ = ports_.clock.now();
        last_recorder_publish_ = recording_since_;
    } else {
        recorder_.take.reset();
        recorder_.last_result = started.error();
        ports_.logger.log(common::LogLevel::kWarning, "録音を始められませんでした");
    }
    detail::bump_revision(recorder_.revision);
    publish_recorder();
    return recorder_.revision;
}

MediaDesk::CommandResult MediaDesk::load(const std::string& path) {
    if (path.empty()) {
        if (player_.file) {
            ports_.player.unload();
            clear_file();
            detail::bump_revision(player_.revision);
            publish_player();
        }
        return player_.revision;
    }
    // UI から任意のパスを受け取らず、エンジンが作った一覧のファイルだけを開く。
    const bool listed = std::any_of(library_.begin(), library_.end(),
                                    [&](const cp::MediaFile& f) { return f.path == path; });
    if (!listed) {
        return common::make_unexpected(ErrorCode::kNotFound);
    }
    auto loaded =
        ports_.player.load(ports_.library.resolve(recorder_.config.folder, path), player_.target);
    clear_file();
    if (loaded) {
        const MediaInfo& info = loaded.value();
        player_.file = path;
        player_.channels = static_cast<std::uint8_t>(info.channels);
        length_frames_ = info.length_frames;
        player_.duration_ms = frames_to_ms(info.length_frames);
        player_.last_result = cp::MediaResult::kOk;
    } else {
        player_.last_result = loaded.error();
    }
    detail::bump_revision(player_.revision);
    publish_player();
    return player_.revision;
}

MediaDesk::CommandResult MediaDesk::set_transport(cp::MediaTransport transport,
                                                  const TargetCheck& is_target) {
    if (!player_.file) {
        return common::make_unexpected(ErrorCode::kInvalidState);
    }
    if (transport == player_.transport) {
        return player_.revision;
    }
    switch (transport) {
        case cp::MediaTransport::kPlaying:
            // 流す先がないまま鳴らし始めると、どこにも出ないのに再生中と表示してしまう。
            if (!is_target(player_.target)) {
                return common::make_unexpected(ErrorCode::kInvalidState);
            }
            ports_.player.play();
            last_position_publish_ = ports_.clock.now();
            break;
        case cp::MediaTransport::kPaused:
            ports_.player.pause();
            break;
        case cp::MediaTransport::kStopped:
            ports_.player.pause();
            ports_.player.seek(0);
            player_.position_ms = 0;
            break;
    }
    player_.transport = transport;
    detail::bump_revision(player_.revision);
    publish_player();
    return player_.revision;
}

MediaDesk::CommandResult MediaDesk::seek(std::uint64_t position_ms) {
    if (!player_.file) {
        return common::make_unexpected(ErrorCode::kInvalidState);
    }
    const std::uint64_t frame = std::min(position_ms * kFramesPerMs, length_frames_);
    ports_.player.seek(frame);
    player_.position_ms = frames_to_ms(frame);
    detail::bump_revision(player_.revision);
    publish_player();
    return player_.revision;
}

MediaDesk::CommandResult MediaDesk::set_target(const std::string& target,
                                               const TargetCheck& is_target) {
    if (!is_target(target)) {
        return common::make_unexpected(ErrorCode::kNotFound);
    }
    if (target != player_.target) {
        ports_.player.set_target(target);
        player_.target = target;
        detail::bump_revision(player_.revision);
        publish_player();
    }
    return player_.revision;
}

MediaDesk::CommandResult MediaDesk::refresh_library() {
    rebuild_library();
    publish_library();
    return player_.revision;
}

void MediaDesk::prune_sources(const SourceLabel& label_of) {
    const auto removed = std::erase_if(
        recorder_.config.sources, [&](const std::string& id) { return !label_of(id).has_value(); });
    if (removed > 0) {
        detail::bump_revision(recorder_.revision);
        publish_recorder();
    }
}

void MediaDesk::on_routing_applied(const TargetCheck& is_target) {
    if (player_.transport != cp::MediaTransport::kPlaying || is_target(player_.target)) {
        return;
    }
    // 別のバスや Master
    // へ暗黙に切り替えると、意図しない出力に音が出る。止めてオペレーターに任せる。
    ports_.player.pause();
    player_.transport = cp::MediaTransport::kPaused;
    detail::bump_revision(player_.revision);
    publish_player();
    notice(cp::NoticeSeverity::kWarning, "media_player_target_removed");
}

void MediaDesk::tick() {
    const auto now = ports_.clock.now();
    if (recorder_.state == cp::RecorderState::kRecording) {
        const RecordingStatus status = ports_.recorder.status();
        update_dropped(status);
        if (status.failure) {
            // 1 本でも書けなくなったら全体を止める。残りのトラックだけ録り続けても、後で
            // 揃えられないテイクになるため。ライブの音声は録音と関係なく流れ続ける。
            stop_recording();
            recorder_.last_result = to_recorder_result(*status.failure);
            detail::bump_revision(recorder_.revision);
            publish_recorder();
            notice(cp::NoticeSeverity::kWarning, "recording_failed");
            ports_.logger.log(common::LogLevel::kWarning, "録音の書き出しに失敗したので止めました");
            rebuild_library();
            publish_library();
        } else if (!last_recorder_publish_ ||
                   now - *last_recorder_publish_ >= kRecorderPublishInterval) {
            update_elapsed();
            last_recorder_publish_ = now;
            publish_recorder();
        }
    }

    if (player_.transport == cp::MediaTransport::kPlaying) {
        const MediaPlayerStatus status = ports_.player.status();
        if (status.failure) {
            ports_.player.pause();
            player_.transport = cp::MediaTransport::kPaused;
            player_.last_result = to_media_result(*status.failure);
            update_position(status.position_frames);
            detail::bump_revision(player_.revision);
            publish_player();
            notice(cp::NoticeSeverity::kWarning, "media_player_failed");
        } else if (status.at_end) {
            // 終わりまで鳴らしたら先頭に戻して止める。もう一度再生を押せば頭から鳴る。
            // エンジンが観測した停止なので revision は進めない。
            ports_.player.pause();
            ports_.player.seek(0);
            player_.transport = cp::MediaTransport::kStopped;
            player_.position_ms = 0;
            publish_player();
        } else if (!last_position_publish_ ||
                   now - *last_position_publish_ >= kMediaPositionPublishInterval) {
            update_position(status.position_frames);
            last_position_publish_ = now;
            publish_player();
        }
    }
}

void MediaDesk::publish_recorder() {
    ports_.ui.publish(cp::RecorderUpdated{recorder_});
}

void MediaDesk::publish_player() {
    ports_.ui.publish(cp::MediaPlayerUpdated{player_});
}

void MediaDesk::publish_library() {
    ports_.ui.publish(cp::MediaLibraryUpdated{library_});
}

void MediaDesk::notice(cp::NoticeSeverity severity, const char* code) {
    cp::Notice n;
    n.severity = severity;
    n.code = code;
    ports_.ui.publish(n);
}

void MediaDesk::stop_recording() {
    update_dropped(ports_.recorder.status());
    update_elapsed();
    ports_.recorder.stop();
    recorder_.state = cp::RecorderState::kStopped;
    recording_since_.reset();
    last_recorder_publish_.reset();
}

void MediaDesk::clear_file() {
    player_.file.reset();
    player_.channels = 0;
    player_.duration_ms = 0;
    player_.position_ms = 0;
    player_.transport = cp::MediaTransport::kStopped;
    length_frames_ = 0;
}

void MediaDesk::rebuild_library() {
    library_.clear();
    for (auto& path : ports_.library.list(recorder_.config.folder, cp::kMaxMediaFiles)) {
        library_.push_back(cp::MediaFile{std::move(path)});
    }
    // 読み込んでいたファイルが一覧から消えても、開いているファイルはそのまま鳴らせる。
    // 次に読み込むときに一覧から選び直してもらう。
}

void MediaDesk::update_dropped(const RecordingStatus& status) {
    const std::size_t n = std::min(status.dropped_frames.size(), recorder_.tracks.size());
    for (std::size_t i = 0; i < n; ++i) {
        recorder_.tracks[i].dropped_ms = frames_to_ms(status.dropped_frames[i]);
    }
}

void MediaDesk::update_elapsed() {
    if (recording_since_) {
        recorder_.elapsed_ms =
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           ports_.clock.now() - *recording_since_)
                                           .count());
    }
}

void MediaDesk::update_position(std::uint64_t frames) {
    player_.position_ms = frames_to_ms(std::min(frames, length_frames_));
}

}  // namespace vocroz::session
