// SPDX-License-Identifier: AGPL-3.0-only
//
// 現在時刻を取得するポート。
// タイムアウトや再接続の判定などで時刻を使うドメインロジックを、
// テストでは FakeClock に差し替えて決定的に動かすために、時刻の取得をこのポートに集約する。
#pragma once

#include <chrono>

namespace vocroz::common {

/// 単調増加する時刻の取得元。
/// 壁時計（システム時刻）はユーザーや NTP の補正で巻き戻るため、経過時間の計測には使わない。
/// 実装はコンポジションルートで生成し、参照で注入する（AGENTS.md 4.3）。
class Clock {
public:
    /// 時刻の型。std::chrono::steady_clock と同じ基準と分解能を使う。
    using TimePoint = std::chrono::steady_clock::time_point;

    virtual ~Clock() = default;

    /// 現在時刻を返す。前回の呼び出しより前の時刻を返すことはない。
    /// @return 単調増加する現在時刻
    /// スレッド安全性: どのスレッドからも同時に呼び出し可能。実装はブロックしない。
    [[nodiscard]] virtual TimePoint now() const noexcept = 0;
};

}  // namespace vocroz::common
