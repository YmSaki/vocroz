// SPDX-License-Identifier: AGPL-3.0-only
//
// Clock ポートの実装。std::chrono::steady_clock をそのまま返す。
// OS の API を直接呼ばず、標準ライブラリだけで書けるので、アダプターのモジュールではなく
// 共通基盤に置き、pa-engine と子機の組み立ての両方から使う。
#pragma once

#include <chrono>

#include "vocroz/common/ports/clock.h"

namespace vocroz::common {

/// std::chrono::steady_clock による Clock。
/// スレッド安全性: 状態を持たないので、どのスレッドからも同時に呼び出し可能。
class SteadyClock final : public Clock {
public:
    /// 現在時刻を返す。
    /// @return steady_clock の現在時刻（巻き戻らない）
    [[nodiscard]] TimePoint now() const noexcept override {
        return std::chrono::steady_clock::now();
    }
};

}  // namespace vocroz::common
