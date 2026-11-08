// SPDX-License-Identifier: AGPL-3.0-only
//
// 製品名とバージョンの表示文字列を生成する共通ユーティリティ。
// すべての実行ファイルの --version 出力をこの書式に統一する。
#pragma once

#include <string>
#include <string_view>

namespace vocroz::common {

/// "<製品名> <実行ファイル名> <バージョン>" 形式の 1 行を返す（改行なし）。
/// @param app_name 実行ファイル名（例: "pa-engine"）
[[nodiscard]] std::string version_line(std::string_view app_name);

/// 任意のバージョンについて、version_line() と同じ書式の 1 行を返す（改行なし）。
/// pa-updater が、新しい版の実行ファイルの --version の出力を照合するために使う。
/// @param app_name 実行ファイル名（例: "pa-engine"）
/// @param version  バージョンの文字列（例: "0.1.0-alpha.2"）
/// @return "<製品名> <app_name> <version>"。スレッド安全（状態を持たない）。
[[nodiscard]] std::string version_line(std::string_view app_name, std::string_view version);

}  // namespace vocroz::common
