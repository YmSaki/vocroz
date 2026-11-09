// SPDX-License-Identifier: AGPL-3.0-only
//
// crash_dialogs.h の実装。

#include "crash_dialogs.h"

#include <windows.h>

namespace vocroz::plugin_scanner::win {

void suppress_crash_dialogs() noexcept {
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
}

}  // namespace vocroz::plugin_scanner::win
