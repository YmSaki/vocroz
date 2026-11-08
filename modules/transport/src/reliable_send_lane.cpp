// SPDX-License-Identifier: AGPL-3.0-only
//
// 信頼制御層の 1 つのレーンの送信側の実装。

#include "reliable_send_lane.h"

#include <algorithm>
#include <bit>
#include <limits>

#include "transport_constants.h"

namespace vocroz::transport {

common::Result<void, TransportError> ReliableSendLane::enqueue(
    std::span<const std::uint8_t> message) {
    if (message.size() > kMaxControlMessageBytes) {
        return common::make_unexpected(TransportError::kPayloadTooLarge);
    }
    if (next_sequence_ == std::numeric_limits<std::uint32_t>::max()) {
        return common::make_unexpected(TransportError::kSequenceExhausted);
    }
    if (pending_.size() >= kMaxQueuedMessages) {
        return common::make_unexpected(TransportError::kQueueFull);
    }
    pending_.push_back(PendingMessage{
        .sequence = next_sequence_,
        .message = std::vector<std::uint8_t>(message.begin(), message.end()),
    });
    ++next_sequence_;
    return {};
}

bool ReliableSendLane::is_valid_ack(const wire::AckFrame& ack) const noexcept {
    if (ack.cumulative > sent_limit_) {
        return false;
    }
    if (ack.selective == 0) {
        return true;
    }
    // 最も上位の 1 のビットが表す連番が、送った範囲に収まっていればよい。
    const auto highest_bit = 63U - static_cast<unsigned>(std::countl_zero(ack.selective));
    const auto highest = static_cast<std::uint64_t>(ack.cumulative) + 1 + highest_bit;
    return highest < sent_limit_;
}

bool ReliableSendLane::covers(const wire::AckFrame& ack, std::uint32_t sequence) noexcept {
    if (sequence < ack.cumulative) {
        return true;
    }
    if (sequence == ack.cumulative) {
        return false;
    }
    const auto bit = static_cast<std::uint64_t>(sequence) - ack.cumulative - 1;
    return bit < 64 && ((ack.selective >> bit) & 1U) != 0;
}

void ReliableSendLane::apply_ack(const wire::AckFrame& ack, TimePoint now, RttEstimator& rtt) {
    for (auto& pending : pending_) {
        if (pending.acknowledged || !pending.sent || !covers(ack, pending.sequence)) {
            continue;
        }
        pending.acknowledged = true;
        if (!pending.retransmitted) {
            rtt.add_sample(now - pending.first_sent);
        }
    }
    // 先頭から続く ACK 済みのメッセージを外し、窓を進める。途中の ACK 済みのものは、
    // 前のメッセージの ACK が届いた時点で外れる。
    while (!pending_.empty() && pending_.front().acknowledged) {
        pending_.pop_front();
    }
}

bool ReliableSendLane::in_window(std::uint32_t sequence) const noexcept {
    return !pending_.empty() &&
           static_cast<std::uint64_t>(sequence) <
               static_cast<std::uint64_t>(pending_.front().sequence) + kReliableWindowMessages;
}

bool ReliableSendLane::append_due(TimePoint now, RttEstimator::Duration timeout,
                                  wire::ControlFrameWriter& writer) {
    for (auto& pending : pending_) {
        if (!in_window(pending.sequence)) {
            break;
        }
        if (pending.acknowledged || (pending.sent && pending.due > now)) {
            continue;
        }
        if (!writer.append_data(wire::DataFrame{
                .lane = lane_, .sequence = pending.sequence, .message = pending.message})) {
            return true;
        }
        if (pending.sent) {
            // 再送のたびにタイムアウトを倍にする。経路が詰まっているときに再送で上乗せしないため。
            pending.retransmitted = true;
            pending.timeout =
                std::min<RttEstimator::Duration>(pending.timeout * 2, kMaxRetransmitTimeout);
        } else {
            pending.sent = true;
            pending.first_sent = now;
            pending.timeout = timeout;
            sent_limit_ = std::max(sent_limit_, pending.sequence + 1);
        }
        pending.due = now + pending.timeout;
    }
    return false;
}

std::optional<ReliableSendLane::TimePoint> ReliableSendLane::next_retransmit_deadline()
    const noexcept {
    std::optional<TimePoint> earliest;
    for (const auto& pending : pending_) {
        if (pending.sent && !pending.acknowledged && (!earliest || pending.due < *earliest)) {
            earliest = pending.due;
        }
    }
    return earliest;
}

}  // namespace vocroz::transport
