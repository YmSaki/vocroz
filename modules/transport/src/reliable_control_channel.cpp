// SPDX-License-Identifier: AGPL-3.0-only
//
// 信頼制御層の実装。レーンごとの送受信は ReliableSendLane と ReliableReceiveLane に任せ、
// ここではパケットの組み立てと、送るきっかけ（送信、受信、時刻）を受け持つ。

#include "vocroz/transport/reliable_control_channel.h"

#include <algorithm>
#include <array>
#include <cstddef>

#include "reliable_receive_lane.h"
#include "reliable_send_lane.h"
#include "rtt_estimator.h"
#include "transport_constants.h"
#include "wire/control_frame_codec.h"

namespace vocroz::transport {

struct ReliableControlChannel::Impl {
    Impl(const common::Clock& clock_source, ControlPacketSink& packet_sink,
         ControlMessageListener& message_listener)
        : clock(clock_source),
          sink(packet_sink),
          listener(message_listener),
          last_sent(clock_source.now()) {}

    // ACK、送るべきメッセージ、キープアライブを、必要なだけのパケットに詰めて送る。
    void flush(common::Clock::TimePoint now, bool keepalive_due);

    const common::Clock& clock;
    ControlPacketSink& sink;
    ControlMessageListener& listener;
    std::array<ReliableSendLane, kControlLaneCount> send_lanes{
        ReliableSendLane(ControlLane::kControl), ReliableSendLane(ControlLane::kMidi)};
    std::array<ReliableReceiveLane, kControlLaneCount> receive_lanes{
        ReliableReceiveLane(ControlLane::kControl), ReliableReceiveLane(ControlLane::kMidi)};
    RttEstimator rtt;
    common::Clock::TimePoint last_sent;
    wire::ControlFrameWriter writer;
};

void ReliableControlChannel::Impl::flush(common::Clock::TimePoint now, bool keepalive_due) {
    bool first = true;
    while (true) {
        writer.clear();
        if (first) {
            // ACK は最初のパケットにだけ載せる。全レーンの ACK は空のパケットに必ず収まる。
            for (auto& lane : receive_lanes) {
                if (lane.ack_pending() && writer.append_ack(lane.ack())) {
                    lane.mark_ack_sent();
                }
            }
        }
        bool more = false;
        for (auto& lane : send_lanes) {
            more = lane.append_due(now, rtt.retransmit_timeout(), writer) || more;
        }
        if (writer.empty() && !(first && keepalive_due)) {
            return;
        }
        // 送れなかったパケットは経路上の欠落と同じに扱い、再送とキープアライブで回復する。
        static_cast<void>(sink.send_control_packet(writer.bytes()));
        last_sent = now;
        first = false;
        if (!more) {
            return;
        }
    }
}

ReliableControlChannel::ReliableControlChannel(const common::Clock& clock, ControlPacketSink& sink,
                                               ControlMessageListener& listener)
    : impl_(std::make_unique<Impl>(clock, sink, listener)) {}

ReliableControlChannel::~ReliableControlChannel() = default;

common::Result<void, TransportError> ReliableControlChannel::send(
    ControlLane lane, std::span<const std::uint8_t> message) {
    auto enqueued = impl_->send_lanes[static_cast<std::size_t>(lane)].enqueue(message);
    if (!enqueued) {
        return enqueued;
    }
    // 低遅延のため、まとめて送るのを待たずにその場で送る。
    impl_->flush(impl_->clock.now(), false);
    return {};
}

common::Result<void, TransportError> ReliableControlChannel::on_control_packet(
    std::span<const std::uint8_t> plaintext) {
    auto frames = wire::parse_control_frames(plaintext);
    if (!frames) {
        return common::make_unexpected(frames.error());
    }
    // 一部だけを適用して状態が中途半端に変わらないよう、ACK をすべて確かめてから適用する。
    for (const auto& ack : frames.value().acks) {
        if (!impl_->send_lanes[static_cast<std::size_t>(ack.lane)].is_valid_ack(ack)) {
            return common::make_unexpected(TransportError::kMalformedPayload);
        }
    }
    const auto now = impl_->clock.now();
    for (const auto& ack : frames.value().acks) {
        impl_->send_lanes[static_cast<std::size_t>(ack.lane)].apply_ack(ack, now, impl_->rtt);
    }
    for (const auto& data : frames.value().data) {
        impl_->receive_lanes[static_cast<std::size_t>(data.lane)].accept(data, impl_->listener);
    }
    // 受け取ったメッセージの ACK と、ACK で窓が空いて送れるようになったメッセージを送る。
    impl_->flush(impl_->clock.now(), false);
    return {};
}

void ReliableControlChannel::tick() {
    const auto now = impl_->clock.now();
    impl_->flush(now, now - impl_->last_sent >= kKeepaliveInterval);
}

common::Clock::TimePoint ReliableControlChannel::next_deadline() const noexcept {
    auto deadline = impl_->last_sent + kKeepaliveInterval;
    for (const auto& lane : impl_->send_lanes) {
        if (const auto due = lane.next_retransmit_deadline()) {
            deadline = std::min(deadline, *due);
        }
    }
    return deadline;
}

std::size_t ReliableControlChannel::unacknowledged_count(ControlLane lane) const noexcept {
    return impl_->send_lanes[static_cast<std::size_t>(lane)].unacknowledged_count();
}

}  // namespace vocroz::transport
