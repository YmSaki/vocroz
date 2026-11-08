// SPDX-License-Identifier: AGPL-3.0-only
//
// ログファイルの出力の設定。コンポジションルートが --data-dir と --log-level から作り、
// make_rotating_file_logger() に渡す（docs/adr/0007-launch-interface.md）。
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "vocroz/common/ports/logger.h"

namespace vocroz::persistence {

/// ログファイルの出力の設定。
struct LogOptions {
    /// ログを置くフォルダ（<--data-dir>\logs）。なければ作る。
    std::filesystem::path directory;
    /// ファイル名の元になる名前（例: "pa-engine"）。同じフォルダに複数のプロセスが書くので、
    /// プロセスとモードごとに変える。ファイルは <base_name>.log、<base_name>.1.log、… になる。
    std::string base_name;
    /// 出力する最も低い重要度（--log-level）。
    common::LogLevel min_level = common::LogLevel::kInfo;
    /// ファイル 1 つの大きさの上限（バイト）。超えそうになったら次のファイルに移る。
    /// 既定値は仕様の例（10MB × 5 ファイル）に合わせる。
    std::uintmax_t max_file_bytes = std::uintmax_t{10} * 1024 * 1024;
    /// 残すファイルの数（現在のファイルを含む）。1 以上。
    int file_count = 5;
};

}  // namespace vocroz::persistence
