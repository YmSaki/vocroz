// SPDX-License-Identifier: AGPL-3.0-only
//
// remote-pa のコンポジションルート（Windows で JUCE を使うビルドだけ）。
// 両方のモードで共有するもの（データフォルダ、ログ、ダイアログの言語、埋め込んだ UI、JUCE の
// メッセージループ）を用意し、起動オプションのモードに応じて親機モード（HostMode）か子機モード
// （client::make_client_app()）を組み立てる（AGENTS.md 4.3、docs/adr/0030-remote-pa-assembly.md）。
// モードは起動ごとに固定し、実行中には切り替えない（AGENTS.md 2.2）。
#pragma once

#include "launch_options.h"

namespace vocroz::pa_desktop {

/// remote-pa の組み立てと実行。
/// スレッド安全性: メインスレッドで作り、run() もメインスレッドから 1 回だけ呼ぶ。
/// メインスレッドが JUCE のメッセージスレッドになる。
class CompositionRoot {
public:
    /// @param options 検証済みの起動オプション（command は kRun）
    explicit CompositionRoot(LaunchOptions options);

    /// 画面を開き、閉じるまで動かす。
    /// @return 終了コード（launch_options.h）
    [[nodiscard]] int run();

private:
    LaunchOptions options_;
};

}  // namespace vocroz::pa_desktop
