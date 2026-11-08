// SPDX-License-Identifier: AGPL-3.0-only
//
// SimulatedNetwork の実装。

#include "vocroz/netsim/simulated_network.h"

#include <utility>

namespace vocroz::netsim {

SimulatedNetwork::SimulatedNetwork(const common::Clock& clock) noexcept : clock_(clock) {}

void SimulatedNetwork::attach(const NodeAddress& address, Receiver receiver) {
    receivers_.insert_or_assign(address, std::move(receiver));
}

void SimulatedNetwork::detach(const NodeAddress& address) {
    receivers_.erase(address);
}

void SimulatedNetwork::set_fault_profile(const NodeAddress& from, const NodeAddress& to,
                                         const FaultProfile& profile) {
    auto& current = link(from, to);
    // 統計と途絶の状態は引き継ぎ、判定だけを新しい設定で作り直す。
    current.injector = FaultInjector(profile);
}

void SimulatedNetwork::begin_outage(const NodeAddress& from, const NodeAddress& to,
                                    OutageMode mode) {
    link(from, to).outage = mode;
}

void SimulatedNetwork::end_outage(const NodeAddress& from, const NodeAddress& to) {
    auto& current = link(from, to);
    current.outage.reset();
    auto held = std::move(current.held);
    current.held.clear();
    for (const auto& datagram : held) {
        schedule(from, to, current, datagram);
    }
}

void SimulatedNetwork::send(const NodeAddress& from, const NodeAddress& to,
                            std::span<const std::uint8_t> datagram) {
    auto& current = link(from, to);
    ++current.stats.sent;
    if (current.outage == OutageMode::kDrop) {
        ++current.stats.dropped_in_outage;
        return;
    }
    if (current.outage == OutageMode::kHold) {
        ++current.stats.held_in_outage;
        current.held.emplace_back(datagram.begin(), datagram.end());
        return;
    }
    schedule(from, to, current, datagram);
}

std::size_t SimulatedNetwork::deliver_due() {
    std::size_t delivered = 0;
    const auto now = clock_.now();
    while (!in_flight_.empty() && in_flight_.top().arrival <= now) {
        // 受信の関数の中から send() が呼ばれてもキューが壊れないよう、取り出してから渡す。
        InFlight packet = in_flight_.top();
        in_flight_.pop();
        auto& stats = link(packet.from, packet.to).stats;
        const auto receiver = receivers_.find(packet.to);
        if (receiver == receivers_.end()) {
            ++stats.undeliverable;
            continue;
        }
        ++stats.delivered;
        ++delivered;
        // 受信の関数が detach() で自分を外しても安全なよう、呼び出しの間はコピーを使う。
        const Receiver receive = receiver->second;
        receive(packet.from, packet.datagram);
    }
    return delivered;
}

std::optional<common::Clock::TimePoint> SimulatedNetwork::next_delivery_time() const {
    if (in_flight_.empty()) {
        return std::nullopt;
    }
    return in_flight_.top().arrival;
}

LinkStats SimulatedNetwork::stats(const NodeAddress& from, const NodeAddress& to) const {
    const auto found = links_.find({from, to});
    return found == links_.end() ? LinkStats{} : found->second.stats;
}

SimulatedNetwork::Link& SimulatedNetwork::link(const NodeAddress& from, const NodeAddress& to) {
    return links_.try_emplace({from, to}, FaultProfile{}).first->second;
}

void SimulatedNetwork::schedule(const NodeAddress& from, const NodeAddress& to, Link& link,
                                std::span<const std::uint8_t> datagram) {
    const FaultDecision decision = link.injector.decide();
    if (decision.lost) {
        ++link.stats.lost;
        return;
    }
    link.stats.duplicated += decision.duplicated ? 1 : 0;
    link.stats.reordered += decision.reordered ? 1 : 0;
    const auto now = clock_.now();
    for (std::size_t i = 0; i < decision.count; ++i) {
        const Delivery& delivery = decision.deliveries[i];
        std::vector<std::uint8_t> copy(datagram.begin(), datagram.end());
        if (delivery.corrupt) {
            ++link.stats.corrupted;
            corrupt_packet(copy, delivery.corrupt_selector);
        }
        in_flight_.push(InFlight{
            .arrival = now + delivery.delay,
            .order = next_order_++,
            .from = from,
            .to = to,
            .datagram = std::move(copy),
        });
    }
}

}  // namespace vocroz::netsim
