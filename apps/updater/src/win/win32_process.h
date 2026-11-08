// SPDX-License-Identifier: AGPL-3.0-only
//
// 子プロセスの起動。起動確認（出力を受け取って待つ）、パッケージの展開（tar.exe）、
// 更新後の再起動（待たない）で使う。
#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vocroz::updater::win {

/// 終了したプロセスの結果。
struct ProcessResult {
    unsigned long exit_code = 0;
    /// 標準出力（UTF-8 として扱う）。
    std::string output;
};

/// プロセスを起動し、終了まで待って標準出力を受け取る。
/// @param executable 実行ファイルの絶対パス（PATH は探さない。同名の偽物を拾わないため）
/// @param args 引数（必要に応じて引用符で囲む）
/// @param timeout 待つ上限。超えたらプロセスを終了させて std::nullopt を返す
/// @return 時間内に終了すれば結果。起動できないか時間切れなら std::nullopt。
[[nodiscard]] std::optional<ProcessResult> run_and_capture(const std::filesystem::path& executable,
                                                           const std::vector<std::wstring>& args,
                                                           std::chrono::milliseconds timeout);

/// プロセスを起動し、待たずに戻る。
/// @return 起動できたら true。
[[nodiscard]] bool start_detached(const std::filesystem::path& executable,
                                  const std::vector<std::wstring>& args);

}  // namespace vocroz::updater::win
