// SPDX-License-Identifier: AGPL-3.0-only
//
// HealthChecker のフェイク。起動確認の成否をテストが決める。
#pragma once

#include "vocroz/update/ports/health_checker.h"

namespace vocroz::update::fakes {

class FakeHealthChecker final : public ports::HealthChecker {
public:
    bool check(const SemanticVersion& /*version*/) override {
        ++calls;
        return healthy;
    }

    bool healthy = true;
    int calls = 0;
};

}  // namespace vocroz::update::fakes
