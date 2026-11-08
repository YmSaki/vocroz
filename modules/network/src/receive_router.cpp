// SPDX-License-Identifier: AGPL-3.0-only
//
// 受信したデータグラムの振り分け。

#include "vocroz/network/receive_router.h"

#include <algorithm>

#include "wire/rendezvous_codec.h"

namespace vocroz::network {

static_assert(wire::kMaxPunchBytes <= kMaxRendezvousDatagramBytes,
              "PUNCH がキューの要素に収まること");

ReceiveRouter::ReceiveRouter(DatagramListener& peer_listener) noexcept
    : peer_listener_(peer_listener) {}

void ReceiveRouter::on_datagram(const UdpAddress& from,
                                std::span<const std::uint8_t> datagram) noexcept {
    if (datagram.empty()) {
        return;
    }
    if (!wire::is_rendezvous_message(datagram)) {
        peer_listener_.on_datagram(from, datagram);
        return;
    }
    if (datagram.size() > kMaxRendezvousDatagramBytes) {
        return;
    }
    RendezvousDatagram message;
    message.from = from;
    message.size = datagram.size();
    std::copy(datagram.begin(), datagram.end(), message.bytes.begin());
    (void)inbox_.try_push(message);  // 満杯なら捨てる。REGISTER の再送で回復する
}

std::optional<RendezvousDatagram> ReceiveRouter::pop_rendezvous() noexcept {
    return inbox_.try_pop();
}

}  // namespace vocroz::network
