// SPDX-License-Identifier: AGPL-3.0-only
//
// command_line.h の実装。

#include "command_line.h"

#include <cstddef>

namespace vocroz::pa_desktop {

std::wstring quote_argument(std::wstring_view argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
        return std::wstring{argument};
    }
    // CommandLineToArgvW の規則: 引用符の直前の \ は 2 つで 1 つの \ になり、\"
    // で引用符そのものになる。 引用符の直前以外の \ はそのまま。くくりの閉じの引用符の直前の \ も 2
    // 倍にする。
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
        } else {
            quoted.append(backslashes, L'\\');
        }
        backslashes = 0;
        quoted += c;
    }
    quoted.append(backslashes * 2, L'\\');
    quoted += L'"';
    return quoted;
}

}  // namespace vocroz::pa_desktop
