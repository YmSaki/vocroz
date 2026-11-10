// SPDX-License-Identifier: AGPL-3.0-only
//
// プラグインのスキャンの記録の JSON と、session の PluginScanRecord との変換。
// 記録のファイルは外部入力なので、ここで型と形を検証する。読めない記録は、呼び出し側が
// 記録なしとして扱い、すべてを再スキャンして作り直す（docs/adr/0032-plugin-catalog-and-loader.md）。
#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "vocroz/session/plugin_scan_record.h"

namespace vocroz::persistence {

/// スキャンの記録を JSON の文字列にする。
/// @param records 記録
/// @return UTF-8 の JSON
[[nodiscard]] std::string encode_plugin_catalog(std::span<const session::PluginScanRecord> records);

/// スキャンの記録の JSON を読む。
/// @param text ファイルの内容
/// @return 記録。形式のバージョン、型、必須のフィールドのどれかが合わなければ std::nullopt
[[nodiscard]] std::optional<std::vector<session::PluginScanRecord>> decode_plugin_catalog(
    std::string_view text);

}  // namespace vocroz::persistence
