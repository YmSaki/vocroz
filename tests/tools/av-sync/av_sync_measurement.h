// SPDX-License-Identifier: AGPL-3.0-only
//
// 計測の誤り。
#pragma once

#include <filesystem>

#include "vocroz/common/result.h"

#include "av_sync_detector.h"

namespace vocroz::tools {
/// 計測の誤り。
enum class AvSyncMeasurementError {
    /// 映像ファイルを読めない
    kVideoFile,
    /// 音声ファイルを読めない
    kAudioFile,
};

/// 計測の結果（組ごとのずれと、その統計）。
struct AvSyncMeasurement {
    AvSyncReport report;
    AvSyncStats stats;
};

/// 映像と音声の順でファイルを読み、対応と統計を返します。
[[nodiscard]] common::Result<AvSyncMeasurement, AvSyncMeasurementError> measure_files(
    const std::filesystem::path& video_path, const std::filesystem::path& audio_path,
    const AvSyncDetectorSettings& settings);
}  // namespace vocroz::tools
