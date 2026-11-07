// SPDX-License-Identifier: AGPL-3.0-only
//
// 製品名とバージョンの表示文字列を組み立てる共通ユーティリティ。
// すべての実行ファイルの --version 出力をこの書式に統一する。
#pragma once

#include <string>
#include <string_view>

namespace vocroz::common {

/// "<製品名> <実行ファイル名> <バージョン>" 形式の 1 行を返す（改行なし）。
/// @param app_name 実行ファイル名（例: "pa-engine"）
[[nodiscard]] std::string version_line(std::string_view app_name);

}  // namespace vocroz::common
