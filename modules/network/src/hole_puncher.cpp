// SPDX-License-Identifier: AGPL-3.0-only
//
// UDP ホールパンチの送信。

#include "hole_puncher.h"

#include <algorithm>

#include "detail/deadline_utils.h"
#include "network_constants.h"

namespace vocroz::network {

HolePuncher::HolePuncher(UdpEndpoint& endpoint) noexcept : endpoint_(endpoint) {}

void HolePuncher::set_datagram(std::span<const std::uint8_t> datagram) {
    datagram_.assign(datagram.begin(), datagram.end());
}

void HolePuncher::punch(const PeerId& peer, std::span<const UdpAddress> candidates,
                        common::Clock::TimePoint now) {
    auto it = std::find_if(targets_.begin(), targets_.end(),
                           [&](const Target& t) { return t.peer == peer; });
    if (it == targets_.end()) {
        targets_.push_back(Target{peer, {}, {}, {}});
        it = targets_.end() - 1;
    }
    it->candidates.assign(candidates.begin(), candidates.end());
    it->window_end = now + kPunchWindow;
    // PUNCH は相手にも同時に届いているので、待たずに送ると両側の送信がそろいやすい。
    send_to_all(*it);
    it->next_send_at = now + kPunchInterval;
}

void HolePuncher::stop_for_address(const UdpAddress& address) {
    std::erase_if(targets_, [&](const Target& t) {
        return std::find(t.candidates.begin(), t.candidates.end(), address) != t.candidates.end();
    });
}

void HolePuncher::stop_all() noexcept {
    targets_.clear();
}

void HolePuncher::poll(common::Clock::TimePoint now) {
    std::erase_if(targets_, [&](const Target& t) { return now >= t.window_end; });
    for (Target& target : targets_) {
        if (now < target.next_send_at) {
            continue;
        }
        send_to_all(target);
        // 遅れて呼ばれても送信をまとめて取り戻さず、次の間隔から数え直す。
        target.next_send_at = now + kPunchInterval;
    }
}

std::optional<common::Clock::TimePoint> HolePuncher::next_deadline() const noexcept {
    std::optional<common::Clock::TimePoint> out;
    for (const Target& target : targets_) {
        out = detail::earliest({out, std::min(target.next_send_at, target.window_end)});
    }
    return out;
}

void HolePuncher::send_to_all(const Target& target) {
    for (const UdpAddress& candidate : target.candidates) {
        // 一部の候補へ送れなくても（IPv4 だけのソケットで IPv6 の候補など）、残りの候補へは送る。
        (void)endpoint_.send_to(candidate, datagram_);
    }
}

}  // namespace vocroz::network
