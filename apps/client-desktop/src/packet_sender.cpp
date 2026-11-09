// SPDX-License-Identifier: AGPL-3.0-only
//
// PacketSender の実装。

#include "packet_sender.h"

#include <algorithm>
#include <utility>

#include "vocroz/transport/media_payload.h"

namespace vocroz::client {

PacketSender::PacketSender(SendWakeup& wakeup) noexcept
    : wakeup_(wakeup), packet_(transport::kMaxPacketBytes) {}

PacketSender::~PacketSender() {
    // 送信スレッドを止めた後に破棄する前提で、まだ取り出していない送信先を片付ける。
    while (const auto pending = paths_.try_pop()) {
        // キューの中だけ生のポインターで持っていた所有権を戻して破棄する。
        std::unique_ptr<SendPath>{*pending}.reset();
    }
}

bool PacketSender::push_frame(const EncodedFrame& frame) noexcept VOCROZ_NONBLOCKING {
    if (!frames_.try_push(frame)) {
        return false;
    }
    wakeup_.notify();
    return true;
}

bool PacketSender::push_control(std::span<const std::uint8_t> plaintext) noexcept {
    if (plaintext.size() > transport::kMaxPlaintextBytes) {
        return false;
    }
    ControlPlaintext item;
    item.generation = control_generation_;
    item.size = plaintext.size();
    std::copy(plaintext.begin(), plaintext.end(), item.bytes.begin());
    if (!control_.try_push(item)) {
        return false;
    }
    wakeup_.notify();
    return true;
}

void PacketSender::set_path(std::unique_ptr<SendPath> path) {
    ++control_generation_;
    if (path) {
        path->generation = control_generation_;
    }
    // nullptr も「送信をやめる」という切り替えとして渡す。
    SendPath* raw = path.release();
    if (!paths_.try_push(raw)) {
        // 切り替えが送信スレッドに届かないほど詰まっている。この接続はあきらめ、
        // 再接続（信頼制御層の途絶の検知から始まる）で回復させる。
        std::unique_ptr<SendPath>{raw}.reset();
        return;
    }
    wakeup_.notify();
}

void PacketSender::apply_path_updates() noexcept {
    while (const auto next = paths_.try_pop()) {
        // 以前の送信先はここで破棄する。送信スレッドはオーディオスレッドではないので解放してよい。
        path_.reset(*next);
    }
}

void PacketSender::pump() noexcept {
    apply_path_updates();
    while (const auto control = control_.try_pop()) {
        if (!path_ || control->generation != path_->generation) {
            dropped_no_path_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        const auto plaintext = std::span<const std::uint8_t>(control->bytes).first(control->size);
        const auto sealed = path_->sealer.seal_control(plaintext, packet_);
        if (!sealed) {
            send_failures_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        send_sealed(sealed.value(), control_sent_);
    }
    while (const auto frame = frames_.try_pop()) {
        if (!path_) {
            dropped_no_path_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        const transport::MediaPayload media{
            .stream_id = kMicStreamId,
            .timestamp = frame->timestamp,
            .mute_epoch = frame->mute_epoch,
            .opus_payload = frame->payload(),
        };
        const auto sealed = path_->sealer.seal_media(media, packet_);
        if (!sealed) {
            send_failures_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        send_sealed(sealed.value(), media_sent_);
    }
}

void PacketSender::send_sealed(std::size_t bytes,
                               std::atomic<std::uint64_t>& sent_counter) noexcept {
    if (!path_->socket->send(std::span<const std::uint8_t>(packet_).first(bytes))) {
        send_failures_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    sent_counter.fetch_add(1, std::memory_order_relaxed);
}

PacketSenderStats PacketSender::stats() const noexcept {
    return PacketSenderStats{
        .media_sent = media_sent_.load(std::memory_order_relaxed),
        .control_sent = control_sent_.load(std::memory_order_relaxed),
        .dropped_no_path = dropped_no_path_.load(std::memory_order_relaxed),
        .send_failures = send_failures_.load(std::memory_order_relaxed),
    };
}

}  // namespace vocroz::client
