// SPDX-License-Identifier: AGPL-3.0-only
//
// コンソールの Ctrl+C、ウィンドウを閉じる操作、ログオフ、シャットダウンで、
// コントロールループに停止を要求する。開発中にコンソールから起動したエンジンを止めるときと、
// OS の終了で後始末（出力デバイスの停止、スリープ抑止の解除）を済ませるために使う。
#pragma once

#include "control_loop.h"

namespace vocroz::engine::win {

/// コンソールの終了の通知でループを止めるハンドラーを登録する。プロセスに 1 回だけ呼ぶ。
/// @param loop 止めるループ。プロセスの終了まで生存させる
/// スレッド安全性: メインスレッドから起動時に 1 回だけ呼ぶ。
void install_console_stop_signal(ControlLoop& loop);

/// 登録したハンドラーを外す。loop を破棄する前に呼ぶ。
void uninstall_console_stop_signal() noexcept;

}  // namespace vocroz::engine::win
