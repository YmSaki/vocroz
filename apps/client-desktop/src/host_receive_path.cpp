// SPDX-License-Identifier: AGPL-3.0-only
//
// HostReceivePath の実装。

#include "host_receive_path.h"

#include <algorithm>
#include <utility>

#include "vocroz/transport/media_frame.h"
#include "vocroz/transport/media_payload.h"
#include "vocroz/transport/packet_header.h"

namespace vocroz::client {

HostReceivePath::HostReceivePath(PeerInbox& handshakes, transport::PlayoutStream& returns) noexcept
    : handshakes_(handshakes), returns_(returns), plaintext_(transport::kMaxPacketBytes) {}

// --- コントロールスレッド ---

bool HostReceivePath::attach(std::uint32_t generation, transport::PacketOpener opener,
                             const network::UdpAddress& remote) {
    reclaim_slots();
    std::size_t slot = 0;
    while (slot < slot_in_use_.size() && slot_in_use_[slot]) {
        ++slot;
    }
    if (slot == slot_in_use_.size()) {
        return false;
    }
    openers_[slot].emplace(std::move(opener));
    Command command;
    command.kind = Command::Kind::kAttach;
    command.generation = generation;
    command.slot = static_cast<std::uint8_t>(slot);
    command.remote = remote;
    // キューに積んだ時点で、置き場の所有は受信スレッドへ移る（積むときの release で、
    // 置いた復号器が受信スレッドから見える）。
    if (!commands_.try_push(command)) {
        openers_[slot].reset();
        return false;
    }
    slot_in_use_[slot] = true;
    return true;
}

bool HostReceivePath::detach(std::uint32_t generation) noexcept {
    Command command;
    command.kind = Command::Kind::kDetach;
    command.generation = generation;
    return commands_.try_push(command);
}

bool HostReceivePath::set_return_mute(std::uint32_t generation, bool muted,
                                      std::uint32_t epoch) noexcept {
    Command command;
    command.kind = Command::Kind::kReturnMute;
    command.generation = generation;
    command.muted = muted;
    command.epoch = epoch;
    return commands_.try_push(command);
}

std::optional<HostPacketEvent> HostReceivePath::pop_event() {
    reclaim_slots();
    return events_.try_pop();
}

void HostReceivePath::reclaim_slots() noexcept {
    while (const auto slot = released_.try_pop()) {
        // 鍵は PacketOpener の破棄のときにゼロで上書きされる。
        openers_[*slot].reset();
        slot_in_use_[*slot] = false;
    }
}

// --- ネットワーク受信スレッド ---

void HostReceivePath::on_datagram(const network::UdpAddress& from,
                                  std::span<const std::uint8_t> datagram) noexcept {
    apply_commands();
    const auto header = transport::parse_packet_header(datagram);
    if (!header) {
        return;
    }
    if (header.value().type == transport::PacketType::kHandshakeResponse) {
        handshakes_.on_datagram(from, datagram);
        return;
    }
    if (active_ && openers_[active_->slot]->connection_id() == header.value().connection_id) {
        handle_packet(from, datagram);
    }
    // 今の接続のものでなければ、古い接続のパケットか偽造なので捨てる。
}

void HostReceivePath::apply_commands() noexcept {
    while (const auto command = commands_.try_pop()) {
        switch (command->kind) {
            case Command::Kind::kAttach:
                release_active();
                active_.emplace(Active{.slot = command->slot,
                                       .generation = command->generation,
                                       .remote = command->remote,
                                       .returns = {}});
                // 新しい接続のタイムスタンプは 0
                // から始まるので、ジッターバッファでは新しい区間にする。
                ++segment_;
                break;
            case Command::Kind::kDetach:
                if (active_ && active_->generation == command->generation) {
                    release_active();
                }
                break;
            case Command::Kind::kReturnMute:
                if (active_ && active_->generation == command->generation &&
                    active_->returns.apply(command->muted, command->epoch) ==
                        transport::MuteUpdate::kUnmuted) {
                    // 解除の後のフレームは、ミュート前とは続かないので新しい区間にする。
                    ++segment_;
                }
                break;
        }
    }
}

void HostReceivePath::release_active() noexcept {
    if (!active_) {
        return;
    }
    // 置き場の数の 2 倍の容量があり、同じ番号を 2 回返すことはないので溢れない。
    static_cast<void>(released_.try_push(active_->slot));
    active_.reset();
}

void HostReceivePath::handle_packet(const network::UdpAddress& from,
                                    std::span<const std::uint8_t> datagram) noexcept {
    const auto opened = openers_[active_->slot]->open(datagram, plaintext_);
    if (!opened) {
        // 偽造、改ざん、再送攻撃のパケット。接続の状態は変えない。
        return;
    }
    authenticated_.fetch_add(1, std::memory_order_release);
    if (from != active_->remote) {
        // NAT のマッピングが変わった。送信先の更新はコントロールスレッドが行う。
        // 渡せなければ、次の認証済みパケットで渡し直す。
        HostPacketEvent event;
        event.kind = HostPacketEvent::Kind::kRemoteChanged;
        event.generation = active_->generation;
        event.from = from;
        if (events_.try_push(event)) {
            active_->remote = from;
        } else {
            dropped_events_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    switch (opened.value().type) {
        case transport::PacketType::kControl: {
            const auto plaintext = opened.value().plaintext;
            HostPacketEvent event;
            event.kind = HostPacketEvent::Kind::kControl;
            event.generation = active_->generation;
            event.size = plaintext.size();
            std::copy(plaintext.begin(), plaintext.end(), event.bytes.begin());
            push_event(event);
            return;
        }
        case transport::PacketType::kMedia:
            handle_return(opened.value().plaintext);
            return;
        case transport::PacketType::kHandshakeInitiation:
        case transport::PacketType::kHandshakeResponse:
            return;
    }
}

void HostReceivePath::handle_return(std::span<const std::uint8_t> plaintext) noexcept {
    const auto media = transport::decode_media_payload(plaintext);
    if (!media || media.value().stream_id != transport::kReturnStreamId) {
        return;
    }
    switch (active_->returns.classify(media.value().mute_epoch)) {
        case transport::FrameVerdict::kDrop:
            // 返送ミュートの前か、ミュート中の世代のフレーム。
            return;
        case transport::FrameVerdict::kAcceptNewSegment:
            // 解除の通知より先に届いた、新しい世代の最初のフレーム。
            ++segment_;
            break;
        case transport::FrameVerdict::kAccept:
            break;
    }
    const auto frame =
        transport::make_media_frame(media.value().timestamp, segment_, media.value().opus_payload);
    if (frame) {
        // 満杯や遅着で捨てたぶんは PlayoutStream の統計に残る。
        static_cast<void>(returns_.enqueue(frame.value()));
    }
}

void HostReceivePath::push_event(const HostPacketEvent& event) noexcept {
    if (!events_.try_push(event)) {
        // 制御パケットは信頼制御層が再送する。
        dropped_events_.fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace vocroz::client
