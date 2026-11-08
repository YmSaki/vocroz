// SPDX-License-Identifier: AGPL-3.0-only
//
// command_line.h の実装。

#include "command_line.h"

namespace vocroz::launcher {

std::wstring_view arguments_of(std::wstring_view command_line) noexcept {
    std::size_t end = 0;
    if (!command_line.empty() && command_line.front() == L'"') {
        // argv[0] の引用符の中ではエスケープを解釈しない（パスに " は使えないため）。
        const std::size_t closing = command_line.find(L'"', 1);
        end = closing == std::wstring_view::npos ? command_line.size() : closing + 1;
    } else {
        end = command_line.find_first_of(L" \t");
        if (end == std::wstring_view::npos) {
            end = command_line.size();
        }
    }
    std::wstring_view rest = command_line.substr(end);
    const std::size_t first = rest.find_first_not_of(L" \t");
    return first == std::wstring_view::npos ? std::wstring_view{} : rest.substr(first);
}

}  // namespace vocroz::launcher
