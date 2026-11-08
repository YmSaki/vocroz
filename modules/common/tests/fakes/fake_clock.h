// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の Clock。テストが明示的に進めたときだけ時刻が進むので、
// タイムアウトなど時刻に依存する処理を実時間を待たずに決定的に検証できる。
#pragma once

#include <atomic>
#include <chrono>

#include "vocroz/common/ports/clock.h"

namespace vocroz::common {

/// advance() で進めたぶんだけ時刻が進む Clock。初期時刻は Clock::TimePoint{}（基準点）。
/// スレッド安全性: now() と advance() はどのスレッドからも同時に呼び出せる。
class FakeClock final : public Clock {
public:
    /// 現在時刻を返す。
    [[nodiscard]] TimePoint now() const noexcept override {
        return TimePoint{TimePoint::duration{ticks_.load(std::memory_order_acquire)}};
    }

    /// 時刻を進める。Clock は単調増加なので、負の値は渡さない。
    /// @param delta 進める時間（0 以上）
    void advance(TimePoint::duration delta) noexcept {
        ticks_.fetch_add(delta.count(), std::memory_order_acq_rel);
    }

private:
    // std::atomic<duration> には fetch_add がないため、刻み数（count）で保持する。
    std::atomic<TimePoint::duration::rep> ticks_{0};
};

}  // namespace vocroz::common
