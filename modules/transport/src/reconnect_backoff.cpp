// SPDX-License-Identifier: AGPL-3.0-only
//
// 再接続のバックオフの実装。

#include "vocroz/transport/reconnect_backoff.h"

#include <algorithm>
#include <array>
#include <chrono>

namespace vocroz::transport {
namespace {

using std::chrono::milliseconds;

// ジッターを掛ける前の間隔。最後の値を上限として繰り返す。
// 倍々で増やしつつ 15 秒で止めるのは、長く待ちすぎると回線が戻っても本番に復帰できないため。
constexpr std::array<milliseconds, 5> kBaseDelays{milliseconds{1000}, milliseconds{2000},
                                                  milliseconds{4000}, milliseconds{8000},
                                                  milliseconds{15000}};

// ジッターの幅（千分率）。間隔を 80% から 120% の間で散らす。
constexpr std::uint64_t kJitterMinPermille = 800;
constexpr std::uint64_t kJitterSpanPermille = 400;

}  // namespace

common::Clock::TimePoint::duration reconnect_delay(std::size_t attempt,
                                                   std::uint32_t random) noexcept {
    const auto base = kBaseDelays[std::min(attempt, kBaseDelays.size() - 1)];
    // 乱数を [0, kJitterSpanPermille] に写す。剰余を使わないのは、偏りなく端まで届かせるため。
    const auto permille = kJitterMinPermille +
                          ((static_cast<std::uint64_t>(random) * (kJitterSpanPermille + 1)) >> 32);
    const auto base_ms = static_cast<std::uint64_t>(base.count());
    const auto jittered = milliseconds{static_cast<milliseconds::rep>(base_ms * permille / 1000)};
    return std::chrono::duration_cast<common::Clock::TimePoint::duration>(jittered);
}

common::Clock::TimePoint::duration ReconnectBackoff::next_delay() noexcept {
    const auto delay = reconnect_delay(attempts_, random_.next_uint32());
    ++attempts_;
    return delay;
}

}  // namespace vocroz::transport
