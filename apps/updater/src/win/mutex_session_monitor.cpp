// SPDX-License-Identifier: AGPL-3.0-only
//
// mutex_session_monitor.h の実装。

#include "win/mutex_session_monitor.h"

#include <windows.h>

#include "vocroz/update/session_lock.h"

#include "win/win32_text.h"

namespace vocroz::updater::win {

MutexSessionMonitor::MutexSessionMonitor(const std::string& data_dir_utf8)
    : mutex_name_(widen(update::session_lock_name(data_dir_utf8))) {}

bool MutexSessionMonitor::is_session_active() {
    HANDLE mutex = ::OpenMutexW(SYNCHRONIZE, FALSE, mutex_name_.c_str());
    if (mutex != nullptr) {
        ::CloseHandle(mutex);
        return true;
    }
    // 存在しないことが確かな場合だけ「稼働していない」とする。アクセス拒否などで
    // 判定できないときは、本番中の可能性を残して稼働中として扱う（フェイルセーフ）。
    return ::GetLastError() != ERROR_FILE_NOT_FOUND;
}

}  // namespace vocroz::updater::win
