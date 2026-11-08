// SPDX-License-Identifier: AGPL-3.0-only
//
// 信頼制御層の 1 つのレーンの受信側の実装。

#include "reliable_receive_lane.h"

#include <utility>

namespace vocroz::transport {

void ReliableReceiveLane::accept(const wire::DataFrame& frame, ControlMessageListener& listener) {
    ack_pending_ = true;
    const std::uint64_t sequence = frame.sequence;
    if (sequence < next_expected_ ||
        sequence >= static_cast<std::uint64_t>(next_expected_) + kReliableWindowMessages) {
        return;
    }
    auto& slot = slots_[slot_of(sequence)];
    if (slot) {
        return;
    }
    slot.emplace(frame.message.begin(), frame.message.end());
    // 先頭からそろったぶんを渡す。渡す前に状態を進めておくのは、listener の中から
    // 送信（と ACK の送出）が呼ばれても、受信状況が正しく見えるようにするため。
    while (slots_[slot_of(next_expected_)]) {
        auto& head = slots_[slot_of(next_expected_)];
        const auto message = std::move(*head);
        head.reset();
        ++next_expected_;
        listener.on_control_message(lane_, message);
    }
}

wire::AckFrame ReliableReceiveLane::ack() const noexcept {
    std::uint64_t selective = 0;
    // ビット i は next_expected_ + 1 + i。窓の中に置けるのは next_expected_ + 窓の幅 - 1
    // までなので、 窓の幅 - 1 ビットを見れば足りる。
    for (std::uint32_t i = 0; i + 1 < kReliableWindowMessages; ++i) {
        const auto sequence = static_cast<std::uint64_t>(next_expected_) + 1 + i;
        if (slots_[slot_of(sequence)]) {
            selective |= std::uint64_t{1} << i;
        }
    }
    return wire::AckFrame{.lane = lane_, .cumulative = next_expected_, .selective = selective};
}

}  // namespace vocroz::transport
