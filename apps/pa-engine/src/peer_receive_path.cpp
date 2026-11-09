// SPDX-License-Identifier: AGPL-3.0-only
//
// PeerReceivePath の実装。

#include "peer_receive_path.h"

#include <algorithm>
#include <utility>

#include "vocroz/transport/constants.h"
#include "vocroz/transport/media_frame.h"
#include "vocroz/transport/media_payload.h"
#include "vocroz/transport/packet_header.h"

namespace vocroz::engine {
namespace {

// 子機のマイクのストリーム。子機は今は 1 本だけを送る（入力ごとのストリームは
// docs/adr/0035-client-input-expansion.md で増える）。
constexpr std::uint16_t kMicStreamId = 0;

}  // namespace

ReceivePeer::ReceivePeer(session::PeerHandle handle_in, transport::PacketOpener opener_in,
                         const network::UdpAddress& remote_in,
                         transport::LivenessMonitor& liveness_in,
                         transport::PlayoutStream& playout_in,
                         network::PeerDatagramSocket& socket_in, common::Clock::TimePoint now)
    : handle(handle_in),
      opener(std::move(opener_in)),
      remote(remote_in),
      liveness(liveness_in),
      playout(playout_in),
      socket(socket_in),
      signal(now),
      plaintext(transport::kMaxPacketBytes) {}

PeerReceivePath::PeerReceivePath(PeerReceiveListener& listener, const common::Clock& clock) noexcept
    : listener_(listener), clock_(clock) {}

void PeerReceivePath::on_datagram(const network::UdpAddress& from,
                                  std::span<const std::uint8_t> datagram) noexcept {
    apply_commands();
    const auto now = clock_.now();
    poll_signals(now);
    const auto header = transport::parse_packet_header(datagram);
    if (!header) {
        return;
    }
    if (header.value().type == transport::PacketType::kHandshakeInitiation) {
        listener_.on_handshake_initiation(from, datagram);
        return;
    }
    if (ReceivePeer* const peer = find(header.value().connection_id)) {
        handle_packet(*peer, from, datagram, now);
    }
}

void PeerReceivePath::apply_commands() noexcept {
    while (const auto command = commands_.try_pop()) {
        ReceivePeer* const peer = command->peer;
        switch (command->kind) {
            case ReceiveCommand::Kind::kAttach: {
                const auto slot = std::find(peers_.begin(), peers_.end(), nullptr);
                if (slot != peers_.end()) {
                    *slot = peer;
                } else {
                    // コントロールスレッドが接続の数を上限までに抑えるので、ここには来ない。
                    // 来たら受け付けずに返す。
                    listener_.on_receive_released(peer->handle);
                }
                break;
            }
            case ReceiveCommand::Kind::kAdmit:
                peer->admitted = true;
                report_signal(*peer);
                break;
            case ReceiveCommand::Kind::kSendMute:
                peer->signal.on_send_mute(command->send_muted, command->mute_epoch, clock_.now());
                report_signal(*peer);
                break;
            case ReceiveCommand::Kind::kDetach: {
                const auto slot = std::find(peers_.begin(), peers_.end(), peer);
                if (slot != peers_.end()) {
                    *slot = nullptr;
                }
                listener_.on_receive_released(peer->handle);
                break;
            }
        }
    }
}

void PeerReceivePath::poll_signals(common::Clock::TimePoint now) noexcept {
    for (ReceivePeer* const peer : peers_) {
        if (peer != nullptr) {
            peer->signal.poll(now);
            report_signal(*peer);
        }
    }
}

void PeerReceivePath::handle_packet(ReceivePeer& peer, const network::UdpAddress& from,
                                    std::span<const std::uint8_t> datagram,
                                    common::Clock::TimePoint now) noexcept {
    const auto opened = peer.opener.open(datagram, peer.plaintext);
    if (!opened) {
        // 偽造、改ざん、再送攻撃のパケット。接続の状態は変えない。
        return;
    }
    peer.liveness.note_packet_received();
    if (from != peer.remote && peer.socket.update_remote(from)) {
        // NAT のマッピングが変わった。認証済みのパケットの送信元へ送り先を移す。
        // 渡せなければ、次の認証済みパケットで渡し直す。
        peer.remote = from;
    }
    switch (opened.value().type) {
        case transport::PacketType::kControl:
            listener_.on_control_packet(peer.handle, opened.value().plaintext);
            return;
        case transport::PacketType::kMedia:
            handle_media(peer, opened.value().plaintext, now);
            return;
        case transport::PacketType::kHandshakeInitiation:
        case transport::PacketType::kHandshakeResponse:
            return;
    }
}

void PeerReceivePath::handle_media(ReceivePeer& peer, std::span<const std::uint8_t> plaintext,
                                   common::Clock::TimePoint now) noexcept {
    if (!peer.admitted) {
        // 承認前の接続のメディアは受け付けない（承認前に音が届かない）。
        return;
    }
    const auto media = transport::decode_media_payload(plaintext);
    if (!media || media.value().stream_id != kMicStreamId) {
        return;
    }
    const bool accepted = peer.signal.on_media_frame(media.value().mute_epoch, now);
    report_signal(peer);
    if (!accepted) {
        // 送信ミュートの前か、ミュート中の世代のフレーム。
        return;
    }
    const auto frame = transport::make_media_frame(
        media.value().timestamp, media.value().mute_epoch, media.value().opus_payload);
    if (frame) {
        // 満杯や遅着で捨てたぶんは PlayoutStream の統計に残る。
        static_cast<void>(peer.playout.enqueue(frame.value()));
    }
}

void PeerReceivePath::report_signal(ReceivePeer& peer) noexcept {
    const auto state = peer.signal.state();
    // 承認前の状態は session が見ない（CONNECTED のときだけ反映する）ので、承認してから知らせる。
    if (!peer.admitted || state == peer.reported) {
        return;
    }
    peer.reported = state;
    listener_.on_signal_state(peer.handle, state);
}

ReceivePeer* PeerReceivePath::find(std::uint32_t connection_id) noexcept {
    for (ReceivePeer* const peer : peers_) {
        if (peer != nullptr && peer->opener.connection_id() == connection_id) {
            return peer;
        }
    }
    return nullptr;
}

}  // namespace vocroz::engine
