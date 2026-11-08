// SPDX-License-Identifier: AGPL-3.0-only
//
// launch_selection.h の実装。

#include "vocroz/update/launch_selection.h"

#include <algorithm>

namespace vocroz::update {

std::vector<SemanticVersion> launch_candidates(const std::optional<InstallPointer>& pointer,
                                               const SemanticVersion& own_version) {
    std::vector<SemanticVersion> candidates;
    const auto add = [&candidates](const SemanticVersion& version) {
        if (std::find(candidates.begin(), candidates.end(), version) == candidates.end()) {
            candidates.push_back(version);
        }
    };
    if (!pointer) {
        add(own_version);
        return candidates;
    }
    if (pointer->current < own_version) {
        add(own_version);
        add(pointer->current);
    } else {
        add(pointer->current);
        add(own_version);
    }
    if (pointer->previous) {
        add(*pointer->previous);
    }
    return candidates;
}

}  // namespace vocroz::update
