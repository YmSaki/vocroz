// SPDX-License-Identifier: AGPL-3.0-only
//
// 既定のインストール先とデータフォルダの解決。
#pragma once

#include <filesystem>
#include <optional>

namespace vocroz::updater::win {

/// 実行中の pa-updater.exe の 1 つ上のフォルダ（app-x.y.z の親 = インストール先）。
[[nodiscard]] std::optional<std::filesystem::path> default_install_dir();

/// %LOCALAPPDATA%\VOCROZ（docs/adr/0007-launch-interface.md の --data-dir の既定）。
[[nodiscard]] std::optional<std::filesystem::path> default_data_dir();

}  // namespace vocroz::updater::win
