// SPDX-License-Identifier: AGPL-3.0-only
//
// 死活監視の実装。

#include "vocroz/transport/liveness_monitor.h"

#include "transport_constants.h"

namespace vocroz::transport {

LivenessMonitor::LivenessMonitor(const common::Clock& clock, LivenessListener& listener) noexcept
    : clock_(clock),
      listener_(listener),
      last_received_ticks_(clock.now().time_since_epoch().count()) {
    static_assert(decltype(last_received_ticks_)::is_always_lock_free,
                  "ネットワーク受信スレッドから待たずに書けるよう、ロックフリーにする");
}

void LivenessMonitor::note_packet_received() noexcept VOCROZ_NONBLOCKING {
    last_received_ticks_.store(clock_.now().time_since_epoch().count(), std::memory_order_release);
}

common::Clock::TimePoint LivenessMonitor::last_received() const noexcept {
    return common::Clock::TimePoint{
        common::Clock::TimePoint::duration{last_received_ticks_.load(std::memory_order_acquire)}};
}

void LivenessMonitor::poll() {
    const auto last = last_received();
    if (timed_out_) {
        if (last > timed_out_at_last_received_) {
            timed_out_ = false;
            listener_.on_peer_resumed();
        }
        return;
    }
    if (clock_.now() - last >= kPeerTimeout) {
        timed_out_ = true;
        timed_out_at_last_received_ = last;
        listener_.on_peer_timed_out();
    }
}

common::Clock::TimePoint LivenessMonitor::next_deadline() const noexcept {
    if (timed_out_) {
        // 再開の判定はキープアライブの間隔で十分に早い。
        return clock_.now() + kKeepaliveInterval;
    }
    return last_received() + kPeerTimeout;
}

}  // namespace vocroz::transport
