// SPDX-License-Identifier: AGPL-3.0-only
//
// PeerPacketSender の実装。

#include "vocroz/transport/peer_packet_sender.h"

#include <algorithm>
#include <utility>

#include "vocroz/transport/media_payload.h"

namespace vocroz::transport {

PeerPacketSender::PeerPacketSender(PacketSealer sealer, DatagramSocket& socket,
                                   std::uint16_t media_stream_id, SendNotifier& notifier)
    : sealer_(std::move(sealer)),
      socket_(socket),
      media_stream_id_(media_stream_id),
      notifier_(notifier),
      packet_(kMaxPacketBytes) {}

common::Result<void, TransportError> PeerPacketSender::send_control_packet(
    std::span<const std::uint8_t> plaintext) noexcept {
    if (plaintext.size() > kMaxPlaintextBytes) {
        return common::make_unexpected(TransportError::kPayloadTooLarge);
    }
    QueuedControlPlaintext item;
    item.size = plaintext.size();
    std::copy(plaintext.begin(), plaintext.end(), item.bytes.begin());
    if (!control_.try_push(item)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return common::make_unexpected(TransportError::kSendFailed);
    }
    notifier_.notify();
    return {};
}

bool PeerPacketSender::push_frame(const MediaFrame& frame) noexcept VOCROZ_NONBLOCKING {
    if (!frames_.try_push(frame)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    notifier_.notify();
    return true;
}

void PeerPacketSender::pump() noexcept {
    // 制御パケットを先に送る。ミュートの通知などを、同じ時点のメディアより遅らせないため。
    while (const auto control = control_.try_pop()) {
        const auto plaintext = std::span<const std::uint8_t>(control->bytes).first(control->size);
        const auto sealed = sealer_.seal_control(plaintext, packet_);
        if (!sealed) {
            send_failures_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        send_sealed(sealed.value(), control_sent_);
    }
    while (const auto frame = frames_.try_pop()) {
        const MediaPayload media{
            .stream_id = media_stream_id_,
            .timestamp = frame->timestamp,
            .mute_epoch = frame->mute_epoch,
            .opus_payload = frame->payload_bytes(),
        };
        const auto sealed = sealer_.seal_media(media, packet_);
        if (!sealed) {
            send_failures_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        send_sealed(sealed.value(), media_sent_);
    }
}

void PeerPacketSender::send_sealed(std::size_t bytes,
                                   std::atomic<std::uint64_t>& sent_counter) noexcept {
    if (!socket_.send(std::span<const std::uint8_t>(packet_).first(bytes))) {
        send_failures_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    sent_counter.fetch_add(1, std::memory_order_relaxed);
}

PeerPacketSenderStats PeerPacketSender::stats() const noexcept {
    return PeerPacketSenderStats{
        .media_sent = media_sent_.load(std::memory_order_relaxed),
        .control_sent = control_sent_.load(std::memory_order_relaxed),
        .dropped = dropped_.load(std::memory_order_relaxed),
        .send_failures = send_failures_.load(std::memory_order_relaxed),
    };
}

}  // namespace vocroz::transport
