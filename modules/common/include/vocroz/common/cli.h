// SPDX-License-Identifier: AGPL-3.0-only
//
// コマンドライン引数を扱う共通ユーティリティ。
// 各実行ファイルの main() が同じ解析処理を重複して持たないようにここへ集約する。
#pragma once

#include <span>
#include <string_view>

namespace vocroz::common {

/// main() の argv を、プログラム名を除いた引数列として扱うためのビュー。
using Args = std::span<const char* const>;

/// argc / argv から、プログラム名（argv[0]）を除いた引数列を作る。
/// @param argc main() に渡された引数の数（0 以上）
/// @param argv main() に渡された引数配列
/// @return argv[1] 以降を指すビュー。argc が 1 以下なら空。
[[nodiscard]] Args make_args(int argc, const char* const* argv) noexcept;

/// 引数列に、指定したフラグと完全一致する要素が含まれるかを返す。
/// @param args  make_args() で作った引数列
/// @param flag  探すフラグ（例: "--version"）
[[nodiscard]] bool has_flag(Args args, std::string_view flag) noexcept;

}  // namespace vocroz::common
