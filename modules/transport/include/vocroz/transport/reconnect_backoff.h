// SPDX-License-Identifier: AGPL-3.0-only
//
// 回線の瞬断から自動で再接続するときの、試行の間隔（バックオフ）。
// 間隔は 1、2、4、8、15 秒で、以降は 15 秒を繰り返す。各間隔に ±20% のジッターを掛け、
// 親機の回線が切れたときに全子機の再接続が同じ時刻に重ならないようにする。
// いつ再接続を始め、いつ諦めるか（再接続猶予）は session が決める。
#pragma once

#include <cstddef>
#include <cstdint>

#include "vocroz/common/ports/clock.h"
#include "vocroz/transport/ports/random_source.h"

namespace vocroz::transport {

/// attempt 回目（0 から数える）の再接続の前に待つ時間を返す。
/// @param attempt 何回目の試行か。0 なら 1 秒、1 なら 2 秒、2 なら 4 秒、3 なら 8 秒、4 以降は 15
/// 秒
/// @param random  ジッターに使う一様な乱数。0 なら -20%、2^32 - 1 なら +20% に近い値になる
/// @return 待つ時間（ミリ秒単位に丸める）
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] common::Clock::TimePoint::duration reconnect_delay(std::size_t attempt,
                                                                 std::uint32_t random) noexcept;

/// 再接続の試行回数を数え、次の試行までの待ち時間を返す。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッド（コントロールスレッド）から使う。
class ReconnectBackoff {
public:
    /// @param random ジッターの乱数の取得元。このオブジェクトより長く生存させる
    explicit ReconnectBackoff(RandomSource& random) noexcept : random_(random) {}

    /// 次の試行までの待ち時間を返し、試行回数を 1 つ進める。
    [[nodiscard]] common::Clock::TimePoint::duration next_delay() noexcept;

    /// 再接続に成功したときに呼び、次の瞬断では 1 秒から始める。
    void reset() noexcept {
        attempts_ = 0;
    }

    /// next_delay() を呼んだ回数（reset() からの試行回数）を返す。
    [[nodiscard]] std::size_t attempts() const noexcept {
        return attempts_;
    }

private:
    RandomSource& random_;
    std::size_t attempts_ = 0;
};

}  // namespace vocroz::transport
