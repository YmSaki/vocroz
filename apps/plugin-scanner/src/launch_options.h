// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-plugin-scanner の起動オプションの定義とパース。引数は内部用で、pa-engine だけが渡す
// （docs/adr/0009-beta-module-layout.md）。
#pragma once

#include <string>
#include <variant>

#include "vocroz/common/cli.h"

namespace vocroz::plugin_scanner {

/// 実行するコマンド。
enum class Command {
    kVersion,  ///< バージョンを表示して終了する
    kScan,  ///< プラグインファイル 1 つをスキャンし、結果を標準出力に書く
};

/// 検証済みの起動オプション。
struct LaunchOptions {
    Command command = Command::kVersion;
    /// kScan のときにスキャンするファイル（UTF-8）。
    std::string plugin_file;
};

/// 引数の誤り。
struct LaunchError {
    /// 開発者向けの説明。
    std::string message;
};

/// 引数をパースする。「--version」か「--scan <ファイル>」のどちらか 1 つだけを受け付ける。
/// 未知の引数を無視すると、pa-engine
/// 側の誤りが別のファイルのスキャンとして記録されるおそれがあるため。
/// @param args プログラム名を除いた引数
/// @return 検証済みのオプションか、誤りの説明。純粋関数。
[[nodiscard]] std::variant<LaunchOptions, LaunchError> parse_launch_options(common::Args args);

}  // namespace vocroz::plugin_scanner
