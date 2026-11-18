// SPDX-License-Identifier: AGPL-3.0-only
//
// 音声と映像のファイルの読み書きの誤り（wav_file と y4m_file で共通に使う）。
#pragma once

#include <filesystem>
#include <span>
#include <vector>

#include "vocroz/common/result.h"

namespace vocroz::tools {
/// 音声と映像のファイルの読み書きの誤り（wav_file と y4m_file で共通に使う）。
enum class MediaFileError {
    /// 開けない、作れない
    kCannotOpen,
    /// 形式が壊れている、想定した形式ではない
    kInvalidFormat,
    /// 形式は正しいが、このツールが扱わない種類
    kUnsupportedFormat,
    /// データが途中で終わっている
    kTruncated,
    /// 書き込みに失敗した
    kWriteFailed,
};

/// 読み込んだ音声。samples は 1 チャンネル目だけ。値は -1.0〜1.0。
struct WavAudio {
    int sample_rate = 0;
    std::vector<float> samples;
};

/// モノラルのサンプルを 16 ビット PCM で書き出します。
[[nodiscard]] common::Result<void, MediaFileError> write_wav_mono16(
    const std::filesystem::path& path, std::span<const float> samples, int sample_rate);
/// PCM または IEEE float WAV から先頭チャンネルを読みます。
[[nodiscard]] common::Result<WavAudio, MediaFileError> read_wav(const std::filesystem::path& path);
}  // namespace vocroz::tools
