// SPDX-License-Identifier: AGPL-3.0-only
//
// PeerReceiveRelay の実装。

#include "peer_receive_relay.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace vocroz::engine {

PeerReceiveRelay::PeerReceiveRelay(ControlLoop& loop) noexcept : loop_(loop) {}

void PeerReceiveRelay::on_handshake_initiation(const network::UdpAddress& from,
                                               std::span<const std::uint8_t> datagram) {
    PeerReceiveListener* const target = target_.load(std::memory_order_acquire);
    if (target == nullptr) {
        return;
    }
    loop_.post([target, from, bytes = std::vector<std::uint8_t>(datagram.begin(), datagram.end())] {
        target->on_handshake_initiation(from, bytes);
    });
}

void PeerReceiveRelay::on_control_packet(session::PeerHandle peer,
                                         std::span<const std::uint8_t> plaintext) {
    PeerReceiveListener* const target = target_.load(std::memory_order_acquire);
    if (target == nullptr) {
        return;
    }
    loop_.post(
        [target, peer, bytes = std::vector<std::uint8_t>(plaintext.begin(), plaintext.end())] {
            target->on_control_packet(peer, bytes);
        });
}

void PeerReceiveRelay::on_signal_state(session::PeerHandle peer, transport::SignalState state) {
    PeerReceiveListener* const target = target_.load(std::memory_order_acquire);
    if (target == nullptr) {
        return;
    }
    loop_.post([target, peer, state] { target->on_signal_state(peer, state); });
}

void PeerReceiveRelay::on_receive_released(session::PeerHandle peer) {
    PeerReceiveListener* const target = target_.load(std::memory_order_acquire);
    if (target == nullptr) {
        return;
    }
    loop_.post([target, peer] { target->on_receive_released(peer); });
}

}  // namespace vocroz::engine
