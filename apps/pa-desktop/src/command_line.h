// SPDX-License-Identifier: AGPL-3.0-only
//
// 子のプロセス（pa-engine）に渡すコマンドラインの組み立て。
// Windows の CreateProcessW は引数を 1 本の文字列で受け取り、子は CommandLineToArgvW と同じ規則で
// 分け直すので、空白や引用符や末尾の \ を含むパス（データフォルダ）をその規則でくくる。
#pragma once

#include <string>
#include <string_view>

namespace vocroz::pa_desktop {

/// 引数 1 つを、CommandLineToArgvW で同じ文字列に戻る形にくくる。
/// 空白、タブ、引用符を含まず空でもない引数はそのまま返す。
/// @param argument くくる引数
/// @return コマンドラインに並べる形の引数。純粋関数。
[[nodiscard]] std::wstring quote_argument(std::wstring_view argument);

}  // namespace vocroz::pa_desktop
