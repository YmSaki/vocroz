// SPDX-License-Identifier: AGPL-3.0-only
//
// ログと診断レポートに書く時刻の書式。
// サポートで両者の時刻を突き合わせるので、書式を 1 か所にまとめる。
#pragma once

#include <chrono>
#include <string>

namespace vocroz::persistence::detail {

/// ISO 8601 の UTC の時刻をミリ秒まで書く。例: 2026-11-08T04:44:52.123Z
/// 利用者の PC のタイムゾーン設定によらず、サポート側で同じ基準で読めるよう UTC にする。
/// @param time 時刻
/// @return 書式化した時刻
[[nodiscard]] std::string format_utc_timestamp(std::chrono::system_clock::time_point time);

}  // namespace vocroz::persistence::detail
