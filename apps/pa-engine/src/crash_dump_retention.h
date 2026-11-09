// SPDX-License-Identifier: AGPL-3.0-only
//
// クラッシュダンプのファイル名の規則と、古いダンプの選び方。
// ダンプは <data-dir>\crash\ に置き、最新の数件だけを残す（docs/adr/0008-runtime-behavior.md）。
// ファイル名に日時を入れ、名前の順が作成の順になるようにして、ファイルの時刻を読まずに選べるようにする。
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace vocroz::engine {

/// pa-engine のクラッシュダンプのファイル名の接頭辞。
inline constexpr std::string_view kCrashDumpPrefix = "pa-engine-";

/// クラッシュダンプのファイル名の拡張子。
inline constexpr std::string_view kCrashDumpExtension = ".dmp";

/// 消すべき古いクラッシュダンプを選ぶ。
/// pa-engine のダンプ（接頭辞と拡張子が一致するもの）だけを対象にし、名前の新しい順に keep
/// 件を残す。 ほかのファイル（利用者が置いたものなど）は選ばない。
/// @param file_names フォルダの中のファイル名
/// @param keep       残す件数
/// @return 消すファイルの名前
/// スレッド安全性: 純粋関数。どのスレッドからも呼び出し可能。
[[nodiscard]] std::vector<std::string> select_stale_crash_dumps(
    const std::vector<std::string>& file_names, std::size_t keep);

}  // namespace vocroz::engine
