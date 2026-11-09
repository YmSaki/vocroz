// SPDX-License-Identifier: AGPL-3.0-only
//
// remote-pa の起動オプションの定義とパース（docs/adr/0007-launch-interface.md）。
// スタートメニューのショートカット、招待 URL（vocroz://）、コマンドラインのどの経路も、この 1 つの
// 規則でモードと引数を決める。矛盾する引数と未知の引数はエラーにし、黙って無視も推測もしない。
#pragma once

#include <optional>
#include <string>
#include <variant>

#include "vocroz/common/cli.h"
#include "vocroz/common/ports/logger.h"

namespace vocroz::pa_desktop {

/// 起動の目的。
enum class Command {
    kRun,      ///< 親機モードか子機モードで起動する
    kVersion,  ///< バージョンを表示して終了する
    kHelp,     ///< 引数の一覧を表示して終了する
};

/// 動作モード。起動ごとに固定し、起動中には切り替えない。
enum class Mode {
    kHost,    ///< 親機モード（pa-engine を操作する）
    kClient,  ///< 子機モード（自分でキャプチャして送る）
};

/// 検証済みの起動オプション。
struct LaunchOptions {
    Command command = Command::kRun;
    Mode mode = Mode::kHost;
    /// 子機モードで入力欄に入れておく招待（--join か vocroz:// の引数）。中身の検証は session
    /// が行う。
    std::optional<std::string> join;
    /// 設定、ログ、キャッシュの置き場所（UTF-8）。省略時は %LOCALAPPDATA%\VOCROZ。
    std::optional<std::string> data_dir;
    /// 表示言語（"ja" か "en"）。省略時は OS の表示言語。
    std::optional<std::string> lang;
    /// ログの詳細度。
    common::LogLevel log_level = common::LogLevel::kInfo;
    /// 起動時に更新を確認するか。
    bool update_check = true;
    /// ランデブーサーバーのベース URL。省略時はビルドの既定値。検証用に別のサーバーを使うときに渡す
    /// （docs/adr/0030-remote-pa-assembly.md）。
    std::optional<std::string> rendezvous_url;
};

/// 引数の誤り。
struct LaunchError {
    /// 利用者（開発者）向けの説明。
    std::string message;
};

/// 引数をパースする。
/// @param args プログラム名を除いた引数
/// @return 検証済みのオプションか、誤りの説明
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] std::variant<LaunchOptions, LaunchError> parse_launch_options(common::Args args);

/// --help で表示する引数の一覧を返す。
[[nodiscard]] std::string launch_options_help();

/// 終了コード（docs/adr/0007-launch-interface.md）。
inline constexpr int kExitOk = 0;
/// 起動後の致命的なエラー。
inline constexpr int kExitFatal = 1;
/// 引数の誤り。
inline constexpr int kExitUsage = 2;
/// 既存のインスタンスに引数を渡して終了した。
inline constexpr int kExitForwarded = 3;

}  // namespace vocroz::pa_desktop
