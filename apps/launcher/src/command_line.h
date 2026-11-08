// SPDX-License-Identifier: AGPL-3.0-only
//
// 起動スタブが受け取ったコマンドラインから、引数の部分だけを取り出す。
// 引数はスタブで解釈せず、そのまま remote-pa に渡す（解釈するのは remote-pa だけ。
// docs/adr/0007-launch-interface.md）。
#pragma once

#include <string_view>

namespace vocroz::launcher {

/// コマンドラインの先頭のプログラム名と、その後ろの空白を除いた残りを返す。
/// プログラム名の区切りは CommandLineToArgvW の argv[0] の規則に従う
/// （先頭が引用符なら次の引用符まで、そうでなければ最初の空白かタブまで）。
/// @param command_line GetCommandLineW() の値
/// @return 引数の部分。引用符やエスケープは元のまま残す。純粋関数。
[[nodiscard]] std::wstring_view arguments_of(std::wstring_view command_line) noexcept;

}  // namespace vocroz::launcher
