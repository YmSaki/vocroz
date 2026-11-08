// SPDX-License-Identifier: AGPL-3.0-only
//
// SessionMonitor のフェイク。テストが稼働中かどうかを直接決める。
#pragma once

#include "vocroz/update/ports/session_monitor.h"

namespace vocroz::update::fakes {

class FakeSessionMonitor final : public ports::SessionMonitor {
public:
    bool is_session_active() override {
        return active;
    }

    bool active = false;
};

}  // namespace vocroz::update::fakes
