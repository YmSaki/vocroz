// SPDX-License-Identifier: AGPL-3.0-only
//
// execution_state_sleep_inhibitor.h の実装。

#include "win/execution_state_sleep_inhibitor.h"

#include <windows.h>

namespace vocroz::engine::win {

ExecutionStateSleepInhibitor::~ExecutionStateSleepInhibitor() {
    set_inhibited(false);
}

void ExecutionStateSleepInhibitor::set_inhibited(bool inhibit) noexcept {
    if (inhibit == inhibited_) {
        return;
    }
    // ES_DISPLAY_REQUIRED は付けない。画面が消えても音は止まらないので、画面のスリープは利用者の
    // 設定のままにする。
    const EXECUTION_STATE state = inhibit ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS;
    if (::SetThreadExecutionState(state) != 0) {
        inhibited_ = inhibit;
    }
}

}  // namespace vocroz::engine::win
