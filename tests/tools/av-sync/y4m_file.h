// SPDX-License-Identifier: AGPL-3.0-only
//
// 読み込んだ映像。frame_lumas はフレームごとの Y 平面の平均（四捨五入）。
#pragma once

#include <cstdint>

#include "wav_file.h"

namespace vocroz::tools {
/// 読み込んだ映像。frame_lumas はフレームごとの Y 平面の平均（四捨五入）。
struct Y4mVideo {
    int width = 0;
    int height = 0;
    /// F の分子 ÷ 分母
    double fps = 0.0;
    std::vector<std::uint8_t> frame_lumas;
};

/// 各フレームを指定の輝度で塗り、4:2:0 の映像を書き出します。
[[nodiscard]] common::Result<void, MediaFileError> write_y4m_flat(
    const std::filesystem::path& path, std::span<const std::uint8_t> frame_lumas, int width,
    int height, int fps);
/// 各フレームの輝度平均とフレームレートを読みます。
[[nodiscard]] common::Result<Y4mVideo, MediaFileError> read_y4m(const std::filesystem::path& path);
}  // namespace vocroz::tools
