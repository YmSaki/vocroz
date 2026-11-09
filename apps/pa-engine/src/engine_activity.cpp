// SPDX-License-Identifier: AGPL-3.0-only
//
// engine_activity.h の実装。

#include "engine_activity.h"

namespace vocroz::engine {

EngineActivity activity_for(control_protocol::SessionState state) noexcept {
    using control_protocol::SessionState;
    switch (state) {
        case SessionState::kIdle:
            return EngineActivity{.session_exists = false, .prevent_sleep = false};
        case SessionState::kCreating:
        case SessionState::kReady:
        case SessionState::kEnding:
        case SessionState::kError:
            return EngineActivity{.session_exists = true, .prevent_sleep = false};
        case SessionState::kActive:
            return EngineActivity{.session_exists = true, .prevent_sleep = true};
    }
    // 未知の値はセッションがあるものとして扱う（更新を止める側に倒す）。
    return EngineActivity{.session_exists = true, .prevent_sleep = false};
}

bool IdleShutdownTimer::should_exit(common::Clock::TimePoint now, bool ui_connected,
                                    bool session_exists) noexcept {
    if (ui_connected || session_exists) {
        idle_since_.reset();
        return false;
    }
    if (!idle_since_) {
        idle_since_ = now;
    }
    return now - *idle_since_ >= timeout_;
}

}  // namespace vocroz::engine
