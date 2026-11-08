// SPDX-License-Identifier: AGPL-3.0-only
//
// 起動スタブが使う Win32 の処理（自分の場所、ファイルの読み込み、プロセスの起動、エラーの表示）。
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace vocroz::launcher::win {

/// 起動スタブ自身のフォルダ（インストール先）。
[[nodiscard]] std::optional<std::filesystem::path> own_directory();

/// ファイルの内容を読む。なければ std::nullopt。
[[nodiscard]] std::optional<std::string> read_text_file(const std::filesystem::path& path);

/// 実行ファイルを、受け取った引数をそのまま付けて起動し、待たずに戻る。
/// @return 起動できたら true。ファイルがない、起動に失敗したら false。
[[nodiscard]] bool launch(const std::filesystem::path& executable, std::wstring_view arguments);

/// エラーのダイアログを出す。
void show_error(std::wstring_view message);

}  // namespace vocroz::launcher::win
