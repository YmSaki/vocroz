// SPDX-License-Identifier: AGPL-3.0-only
//
// update_policy.h の実装。

#include "vocroz/update/update_policy.h"

namespace vocroz::update {

ApplyDecision decide_apply(const ApplyContext& context) {
    if (context.session_active) {
        return ApplyDecision::kBlockedBySession;
    }
    if (!context.user_requested) {
        return ApplyDecision::kNotRequested;
    }
    if (!(context.current < context.candidate)) {
        return ApplyDecision::kNotNewer;
    }
    return ApplyDecision::kApply;
}

}  // namespace vocroz::update
