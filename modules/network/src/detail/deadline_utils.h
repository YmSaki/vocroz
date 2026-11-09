// SPDX-License-Identifier: AGPL-3.0-only
//
// 複数の締め切りのうち最も早いものを選ぶ。next_deadline() を持つクラス（ClientRendezvous、
// HostRendezvous）が、内部の部品の締め切りをまとめるのに使う。
#pragma once

#include <initializer_list>
#include <optional>

#include "vocroz/common/ports/clock.h"

namespace vocroz::network::detail {

/// 締め切りのうち最も早いものを返す。すべて std::nullopt なら std::nullopt。
/// @param deadlines 締め切り
[[nodiscard]] inline std::optional<common::Clock::TimePoint> earliest(
    std::initializer_list<std::optional<common::Clock::TimePoint>> deadlines) noexcept {
    std::optional<common::Clock::TimePoint> out;
    for (const auto& deadline : deadlines) {
        if (deadline && (!out || *deadline < *out)) {
            out = deadline;
        }
    }
    return out;
}

}  // namespace vocroz::network::detail
