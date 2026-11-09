// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の PlayoutControl。反映した目標を記録する。
#pragma once

#include <chrono>
#include <optional>

#include "vocroz/session/ports/playout_control.h"

namespace vocroz::session {

/// 反映した目標レイテンシーを記録する PlayoutControl。
class FakePlayoutControl final : public PlayoutControl {
public:
    void set_latency_target(std::chrono::microseconds target) override {
        target_ = target;
        ++calls_;
    }

    /// 最後に反映した目標。まだ反映していなければ std::nullopt。
    [[nodiscard]] std::optional<std::chrono::microseconds> target() const noexcept {
        return target_;
    }

    [[nodiscard]] int calls() const noexcept {
        return calls_;
    }

private:
    std::optional<std::chrono::microseconds> target_;
    int calls_ = 0;
};

}  // namespace vocroz::session
