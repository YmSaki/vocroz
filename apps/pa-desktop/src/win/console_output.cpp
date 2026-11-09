// SPDX-License-Identifier: AGPL-3.0-only
//
// console_output.h の実装。

#include "win/console_output.h"

#include <cstdio>

#include <windows.h>

namespace vocroz::pa_desktop::win {

void attach_parent_console() {
    const HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (output != nullptr && output != INVALID_HANDLE_VALUE) {
        return;
    }
    if (!::AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;
    }
    // 標準出力をつなぎ直せなくても起動は続ける（表示が出ないだけ）。
    FILE* stream = nullptr;
    static_cast<void>(::freopen_s(&stream, "CONOUT$", "w", stdout));
    static_cast<void>(::freopen_s(&stream, "CONOUT$", "w", stderr));
}

}  // namespace vocroz::pa_desktop::win
