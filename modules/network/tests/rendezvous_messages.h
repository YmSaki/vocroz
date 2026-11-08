// SPDX-License-Identifier: AGPL-3.0-only
//
// テストで、ランデブーサーバーが送る REGISTERED と PUNCH を組み立てるヘルパー。
// 形式は infra/rendezvous/internal/udpapi/wire.go の Append と同じ。クライアントにはサーバー側の
// シリアライザーがないので、テストの中だけで持つ。
#pragma once

#include <cstdint>
#include <vector>

#include "vocroz/network/receive_router.h"
#include "vocroz/network/rendezvous_types.h"

#include "gateway_responses.h"
#include "wire/rendezvous_codec.h"

namespace vocroz::network::test {

inline void push_candidate(std::vector<std::uint8_t>& out, const UdpAddress& address) {
    if (address.is_ipv4()) {
        out.push_back(4);
        push_u16(out, address.port);
        const auto v4 = address.ipv4();
        out.insert(out.end(), v4.begin(), v4.end());
    } else {
        out.push_back(6);
        push_u16(out, address.port);
        out.insert(out.end(), address.ip.begin(), address.ip.end());
    }
}

inline std::vector<std::uint8_t> header(std::uint8_t type, const wire::TxId& tx_id) {
    std::vector<std::uint8_t> out = {'V', 'R', 'Z', 1, type};
    out.insert(out.end(), tx_id.begin(), tx_id.end());
    return out;
}

/// REGISTERED。
inline std::vector<std::uint8_t> registered(const wire::TxId& tx_id, bool capacity_exceeded,
                                            bool peer_present, const UdpAddress& reflexive) {
    auto out = header(2, tx_id);
    out.push_back(capacity_exceeded ? 1 : 0);
    out.push_back(peer_present ? 1 : 0);
    push_candidate(out, reflexive);
    return out;
}

/// PUNCH。
inline std::vector<std::uint8_t> punch(const wire::TxId& tx_id, const RendezvousId& rendezvous_id,
                                       const PeerId& peer_id, RendezvousRole peer_role,
                                       const std::vector<UdpAddress>& candidates) {
    auto out = header(3, tx_id);
    out.insert(out.end(), rendezvous_id.begin(), rendezvous_id.end());
    out.insert(out.end(), peer_id.begin(), peer_id.end());
    out.push_back(static_cast<std::uint8_t>(peer_role));
    out.push_back(static_cast<std::uint8_t>(candidates.size()));
    for (const auto& candidate : candidates) {
        push_candidate(out, candidate);
    }
    return out;
}

/// ReceiveRouter がコントロールスレッドへ渡す形に包む。
inline RendezvousDatagram from(const UdpAddress& source, const std::vector<std::uint8_t>& bytes) {
    RendezvousDatagram datagram;
    datagram.from = source;
    datagram.size = bytes.size();
    std::copy(bytes.begin(), bytes.end(), datagram.bytes.begin());
    return datagram;
}

/// REGISTER の txid（ヘッダーの 5〜12 バイト目）を取り出す。
inline wire::TxId tx_id_of(const std::vector<std::uint8_t>& register_bytes) {
    wire::TxId tx_id{};
    std::copy(register_bytes.begin() + 5, register_bytes.begin() + 13, tx_id.begin());
    return tx_id;
}

}  // namespace vocroz::network::test
