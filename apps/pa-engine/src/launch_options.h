// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-engine の起動オプションの定義とパース。
// pa-engine は remote-pa（親機モード）が起動する内部プロセスで、受け付けるのは --data-dir、
// --log-level、--version だけ（docs/adr/0007-launch-interface.md）。パイプの名前は --data-dir
// から導出するので、引数では受け取らない。
#pragma once

#include <optional>
#include <string>
#include <variant>

#include "vocroz/common/cli.h"
#include "vocroz/common/ports/logger.h"

namespace vocroz::engine {

/// 実行するコマンド。
enum class Command {
    kRun,      ///< エンジンとして動く
    kVersion,  ///< バージョンを表示して終了する
};

/// 検証済みの起動オプション。
struct LaunchOptions {
    Command command = Command::kRun;
    /// データフォルダ（UTF-8）。省略時は %LOCALAPPDATA%\VOCROZ。
    std::optional<std::string> data_dir;
    /// これより低い重要度のログを出さない。既定は info。
    common::LogLevel log_level = common::LogLevel::kInfo;
};

/// 引数の誤り。
struct LaunchError {
    /// 開発者向けの説明（pa-engine は利用者が直接起動しないため）。
    std::string message;
};

/// 引数をパースする。未知の引数と、同じ引数の重複はエラーにする。
/// 黙って無視すると、remote-pa 側の誤り（別の --data-dir を渡すなど）に気付けず、
/// UI とつながらないエンジンが起動するため。
/// @param args プログラム名を除いた引数
/// @return 検証済みのオプションか、誤りの説明
/// スレッド安全性: 純粋関数。どのスレッドからも呼び出し可能。
[[nodiscard]] std::variant<LaunchOptions, LaunchError> parse_launch_options(common::Args args);

}  // namespace vocroz::engine
