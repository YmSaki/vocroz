// SPDX-License-Identifier: AGPL-3.0-only
//
// 既定のデータフォルダ、実行ファイルのフォルダ、VST3 の標準の場所の解決。
#pragma once

#include <filesystem>
#include <optional>
#include <vector>

namespace vocroz::engine::win {

/// %LOCALAPPDATA%\VOCROZ（docs/adr/0007-launch-interface.md の --data-dir の既定）。
/// pa-updater（apps/updater/src/win/known_paths.cpp）と同じ規則で、remote-pa と pa-updater が
/// 同じエンジンとパイプを指すようにする。
/// @return 既定のデータフォルダ。既知のフォルダを取得できなければ std::nullopt
[[nodiscard]] std::optional<std::filesystem::path> default_data_dir();

/// この実行ファイルがあるフォルダ。同梱のスキャン用ヘルパー（pa-plugin-scanner）を探すのに使う。
/// @return フォルダ。取得できなければ std::nullopt
[[nodiscard]] std::optional<std::filesystem::path> executable_directory();

/// VST3 のプラグインの標準の場所（全ユーザー用の %CommonProgramFiles%\VST3 と、ユーザーごとの
/// %LOCALAPPDATA%\Programs\Common\VST3）。探す場所を選ぶ画面はまだないので、この 2 つを探す。
/// @return 取得できた場所。存在しない場所も含む（列挙で飛ばす）
[[nodiscard]] std::vector<std::filesystem::path> vst3_directories();

}  // namespace vocroz::engine::win
