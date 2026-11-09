// SPDX-License-Identifier: AGPL-3.0-only
//
// 利用者がサポートへ送るための、マスキング済みの診断レポートを書き出す。
// ログと設定を 1 つのテキストファイルにまとめ、秘密情報と個人情報を伏せる。
// 決めたことは docs/adr/0027-persistence.md にある。
// 表示名とセッション名は形から見分けられないので値ごと伏せ、ログの鍵や招待コード、
// グローバルな IP アドレス、ユーザー名を含むパスはログファイルへの出力と同じ規則で伏せる。
#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "vocroz/common/result.h"

namespace vocroz::persistence {

/// 診断レポートに入れる情報。コンポジションルートが集めて渡す。
struct DiagnosticReportInput {
    /// 作成した時刻（UTC で書く）。
    std::chrono::system_clock::time_point created_at;
    /// 製品のバージョンの表示（common の version_info の文字列）。
    std::string product_version;
    /// OS やデバイスなどの環境の説明。1 要素を 1 行として書く。秘密情報を含めないこと
    /// （念のため、書く前にログと同じ規則で伏せる）。
    std::vector<std::string> environment;
    /// 設定ファイルのあるフォルダ（--data-dir）。
    std::filesystem::path data_dir;
    /// ログファイルのあるフォルダ。中の *.log を新しいものから上限まで入れる。
    std::filesystem::path log_dir;
};

/// 診断レポートを書けなかった理由。
enum class DiagnosticReportError {
    kWriteFailed,  ///< 出力先に書けなかった
};

/// 診断レポートを output に書く。読めない設定やログは、読めなかったことをレポートに書いて続ける。
/// @param input  レポートに入れる情報
/// @param output 出力先のファイル。既にあれば上書きする
/// @return 書けなければ理由
/// スレッド安全性: コントロールスレッドから呼ぶ。ファイル I/O でブロックする。
[[nodiscard]] common::Result<void, DiagnosticReportError> write_diagnostic_report(
    const DiagnosticReportInput& input, const std::filesystem::path& output);

}  // namespace vocroz::persistence
