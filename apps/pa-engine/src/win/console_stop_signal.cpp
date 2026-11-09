// SPDX-License-Identifier: AGPL-3.0-only
//
// console_stop_signal.h の実装。

#include "win/console_stop_signal.h"

#include <atomic>

#include <windows.h>

namespace vocroz::engine::win {
namespace {

// コンソールのハンドラーは引数で文脈を受け取れないので、止めるループをここに置く。
// 書くのは登録と解除のときだけで、ハンドラーのスレッドは読むだけ。
std::atomic<ControlLoop*> g_loop{nullptr};

BOOL WINAPI on_console_event(DWORD event) {
    switch (event) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            if (ControlLoop* loop = g_loop.load()) {
                loop->request_stop();
                return TRUE;
            }
            return FALSE;
        default:
            return FALSE;
    }
}

}  // namespace

void install_console_stop_signal(ControlLoop& loop) {
    g_loop.store(&loop);
    ::SetConsoleCtrlHandler(on_console_event, TRUE);
}

void uninstall_console_stop_signal() noexcept {
    ::SetConsoleCtrlHandler(on_console_event, FALSE);
    g_loop.store(nullptr);
}

}  // namespace vocroz::engine::win
