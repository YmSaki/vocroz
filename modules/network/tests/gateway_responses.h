// SPDX-License-Identifier: AGPL-3.0-only
//
// テストで使う、ゲートウェイの PCP と NAT-PMP の応答のバイト列。
// パーサー（src/wire/）を使わずに RFC 6887 と RFC 6886 の形式どおりに組み立て、
// パーサーと手順のテストが、実装と同じ誤りを共有しないようにする。
#pragma once

#include <cstdint>
#include <vector>

#include "vocroz/network/udp_address.h"

#include "wire/pcp_codec.h"

namespace vocroz::network::test {

inline void push_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

inline void push_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    push_u16(out, static_cast<std::uint16_t>(v >> 16));
    push_u16(out, static_cast<std::uint16_t>(v));
}

/// PCP の MAP 応答（RFC 6887 の 11.1 と 7.2 の形式）。
inline std::vector<std::uint8_t> pcp_map_response(std::uint8_t result, const wire::PcpNonce& nonce,
                                                  std::uint16_t internal_port,
                                                  const UdpAddress& external,
                                                  std::uint32_t lifetime) {
    std::vector<std::uint8_t> out = {2, 0x81, 0, result};
    push_u32(out, lifetime);
    push_u32(out, 1234);  // epoch
    out.insert(out.end(), 12, 0);
    out.insert(out.end(), nonce.begin(), nonce.end());
    out.push_back(17);  // UDP
    out.insert(out.end(), 3, 0);
    push_u16(out, internal_port);
    push_u16(out, external.port);
    out.insert(out.end(), external.ip.begin(), external.ip.end());
    return out;
}

/// PCP に対応していない NAT-PMP のゲートウェイが、PCP の要求に返す「バージョン非対応」の応答。
inline std::vector<std::uint8_t> nat_pmp_unsupported_version() {
    std::vector<std::uint8_t> out = {0, 129};
    push_u16(out, 1);
    push_u32(out, 1234);
    return out;
}

/// NAT-PMP の外部アドレスの応答。
inline std::vector<std::uint8_t> nat_pmp_external_address(std::uint16_t result,
                                                          const Ipv4Bytes& address) {
    std::vector<std::uint8_t> out = {0, 128};
    push_u16(out, result);
    push_u32(out, 1234);
    out.insert(out.end(), address.begin(), address.end());
    return out;
}

/// NAT-PMP の UDP の転送の応答。
inline std::vector<std::uint8_t> nat_pmp_map(std::uint16_t result, std::uint16_t internal_port,
                                             std::uint16_t external_port, std::uint32_t lifetime) {
    std::vector<std::uint8_t> out = {0, 129};
    push_u16(out, result);
    push_u32(out, 1234);
    push_u16(out, internal_port);
    push_u16(out, external_port);
    push_u32(out, lifetime);
    return out;
}

}  // namespace vocroz::network::test
