// SPDX-License-Identifier: AGPL-3.0-only
//
// PeerInbox の実装。

#include "peer_inbox.h"

#include <algorithm>

namespace vocroz::client {

void PeerInbox::on_datagram(const network::UdpAddress& from,
                            std::span<const std::uint8_t> datagram) noexcept {
    if (datagram.size() > transport::kMaxPacketBytes) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    PeerDatagram item;
    item.from = from;
    item.size = datagram.size();
    std::copy(datagram.begin(), datagram.end(), item.bytes.begin());
    if (!inbox_.try_push(item)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace vocroz::client
