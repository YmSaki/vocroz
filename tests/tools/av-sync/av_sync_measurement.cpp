// SPDX-License-Identifier: AGPL-3.0-only
//
// 試験ファイルから光とクリックの時間差を求めます。
#include "av_sync_measurement.h"

#include "wav_file.h"
#include "y4m_file.h"

namespace vocroz::tools {
common::Result<AvSyncMeasurement, AvSyncMeasurementError> measure_files(
    const std::filesystem::path& video_path, const std::filesystem::path& audio_path,
    const AvSyncDetectorSettings& settings) {
    auto video = read_y4m(video_path);
    if (!video)
        return common::make_unexpected(AvSyncMeasurementError::kVideoFile);
    auto audio = read_wav(audio_path);
    if (!audio)
        return common::make_unexpected(AvSyncMeasurementError::kAudioFile);
    const auto flashes =
        detect_video_onsets_ms(video.value().frame_lumas, video.value().fps, settings);
    const auto clicks =
        detect_audio_onsets_ms(audio.value().samples, audio.value().sample_rate, settings);
    auto report = pair_onsets(flashes, clicks, settings);
    auto stats = summarize(report.offsets_ms);
    return AvSyncMeasurement{std::move(report), stats};
}
}  // namespace vocroz::tools
