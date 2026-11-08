// SPDX-License-Identifier: AGPL-3.0-only
//
// 1 つの相手へ送る DatagramSocket。

#include "vocroz/network/peer_datagram_socket.h"

namespace vocroz::network {

PeerDatagramSocket::PeerDatagramSocket(UdpEndpoint& endpoint, const UdpAddress& remote) noexcept
    : endpoint_(endpoint), remote_(remote) {}

common::Result<void, transport::TransportError> PeerDatagramSocket::send(
    std::span<const std::uint8_t> datagram) noexcept {
    // 複数の更新がたまっていれば、最後のものが最新の送信元なのでそれを使う。
    while (const auto update = updates_.try_pop()) {
        remote_ = *update;
    }
    if (!endpoint_.send_to(remote_, datagram)) {
        return common::make_unexpected(transport::TransportError::kSendFailed);
    }
    return {};
}

bool PeerDatagramSocket::update_remote(const UdpAddress& remote) noexcept {
    return updates_.try_push(remote);
}

}  // namespace vocroz::network
