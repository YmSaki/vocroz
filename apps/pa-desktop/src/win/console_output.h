// SPDX-License-Identifier: AGPL-3.0-only
//
// コマンドラインからの起動で、--version、--help、引数の誤りを表示するための出力先の用意。
// remote-pa はウィンドウのアプリ（Windows のサブシステム）なので、コマンドプロンプトから
// 起動しても標準出力がない。親のコンソールがあればそこにつなぐ。
#pragma once

namespace vocroz::pa_desktop::win {

/// 標準出力と標準エラーがなければ、親のプロセスのコンソールにつなぐ。
/// 出力先がリダイレクトされているとき（テストやスクリプト）は何もしない。
/// スレッド安全性: main() の最初の出力の前に、メインスレッドから呼ぶ。
void attach_parent_console();

}  // namespace vocroz::pa_desktop::win
