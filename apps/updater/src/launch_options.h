// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-updater の起動オプションの定義とパース。引数は内部用で、remote-pa だけが渡す
// （docs/adr/0009-installer-and-updater.md）。
#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "vocroz/common/cli.h"

namespace vocroz::updater {

/// 実行するコマンド。
enum class Command {
    /// バージョンを表示して終了する。
    kVersion,
    /// 更新を確認し、ダウンロードして検証する。
    kCheck,
    /// 検証済みの更新を適用する。
    kApply,
};

/// 検証済みの起動オプション。
struct LaunchOptions {
    Command command = Command::kVersion;
    /// --apply の成功後に起動スタブを起動するか。
    bool restart = false;
    /// 起動スタブに渡す引数（"--" より後ろ）。親機と子機のどちらで再起動するかを引き継ぐため。
    std::vector<std::string> restart_args;
    /// インストール先（UTF-8）。省略時は実行ファイルの 1 つ上のフォルダ。
    std::optional<std::string> install_dir;
    /// データフォルダ（UTF-8）。省略時は %LOCALAPPDATA%\VOCROZ。
    std::optional<std::string> data_dir;
};

/// 引数の誤り。
struct LaunchError {
    /// 利用者（開発者）向けの説明。
    std::string message;
};

/// 引数をパースする。コマンドはちょうど 1 つ必要で、未知の引数や矛盾する組み合わせはエラーにする。
/// 黙って無視すると、remote-pa 側の誤りで意図しない動作（例: 確認のつもりで適用）になるため。
/// @param args プログラム名を除いた引数
/// @return 検証済みのオプションか、誤りの説明。純粋関数。
[[nodiscard]] std::variant<LaunchOptions, LaunchError> parse_launch_options(common::Args args);

}  // namespace vocroz::updater
