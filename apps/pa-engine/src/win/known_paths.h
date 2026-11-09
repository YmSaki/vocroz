// SPDX-License-Identifier: AGPL-3.0-only
//
// 既定のデータフォルダの解決。
#pragma once

#include <filesystem>
#include <optional>

namespace vocroz::engine::win {

/// %LOCALAPPDATA%\VOCROZ（docs/adr/0007-launch-interface.md の --data-dir の既定）。
/// pa-updater（apps/updater/src/win/known_paths.cpp）と同じ規則で、remote-pa と pa-updater が
/// 同じエンジンとパイプを指すようにする。
/// @return 既定のデータフォルダ。既知のフォルダを取得できなければ std::nullopt
[[nodiscard]] std::optional<std::filesystem::path> default_data_dir();

}  // namespace vocroz::engine::win
