// SPDX-License-Identifier: AGPL-3.0-only
//
// remote-pa が使う既知のフォルダの解決。
#pragma once

#include <filesystem>
#include <optional>

namespace vocroz::pa_desktop::win {

/// %LOCALAPPDATA%\VOCROZ（docs/adr/0007-launch-interface.md の --data-dir の既定）。
/// pa-engine と pa-updater（各アプリの src/win/known_paths.cpp）と同じ規則にし、remote-pa と
/// 起動した pa-engine が同じデータフォルダとパイプを指すようにする。
/// @return 既定のデータフォルダ。既知のフォルダを取得できなければ std::nullopt
[[nodiscard]] std::optional<std::filesystem::path> default_data_dir();

/// 実行中の remote-pa.exe があるフォルダ。pa-engine.exe は同じフォルダ（バージョン別フォルダ）に
/// 置くので（docs/adr/0013-installer-and-updater.md）、起動するエンジンをここから探す。
/// @return フォルダ。取得できなければ std::nullopt
[[nodiscard]] std::optional<std::filesystem::path> executable_directory();

}  // namespace vocroz::pa_desktop::win
