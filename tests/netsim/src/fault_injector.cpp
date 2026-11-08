// SPDX-License-Identifier: AGPL-3.0-only
//
// FaultInjector の実装。

#include "vocroz/netsim/fault_injector.h"

namespace vocroz::netsim {

FaultInjector::FaultInjector(const FaultProfile& profile) noexcept
    : profile_(profile), random_(profile.seed) {}

FaultDecision FaultInjector::decide() noexcept {
    // 障害を起こすかどうかに関係なく、1 パケットで同じ数の乱数を消費する。
    // 例えば欠損率だけを変えたときに、遅延の揺らぎの列がずれて別の条件にならないようにするため。
    const bool lost = random_.chance(profile_.loss);
    const auto jitter_us = static_cast<std::int64_t>(random_.below(
        static_cast<std::uint64_t>(profile_.jitter.count() > 0 ? profile_.jitter.count() : 0)));
    const bool reordered = random_.chance(profile_.reorder);
    const bool duplicated = random_.chance(profile_.duplicate);
    const bool corrupted = random_.chance(profile_.corrupt);
    const std::uint64_t selector = random_.next_u64();

    FaultDecision decision;
    if (lost) {
        decision.lost = true;
        return decision;
    }
    const std::chrono::microseconds base = profile_.delay + std::chrono::microseconds{jitter_us};
    decision.reordered = reordered;
    decision.deliveries[0] = Delivery{
        .delay = reordered ? base + profile_.reorder_delay : base,
        .corrupt = corrupted,
        .corrupt_selector = selector,
    };
    decision.count = 1;
    if (duplicated) {
        // 2 つ目は経路上の再送や複製を模して、1 つ目より遅れて届ける。中身は壊さない。
        decision.duplicated = true;
        decision.deliveries[1] = Delivery{.delay = base + profile_.reorder_delay};
        decision.count = 2;
    }
    return decision;
}

void corrupt_packet(std::span<std::uint8_t> packet, std::uint64_t selector) noexcept {
    if (packet.empty()) {
        return;
    }
    const std::uint64_t bit = selector % (packet.size() * 8U);
    packet[bit / 8U] ^= static_cast<std::uint8_t>(1U << (bit % 8U));
}

}  // namespace vocroz::netsim
