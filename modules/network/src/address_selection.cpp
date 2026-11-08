// SPDX-License-Identifier: AGPL-3.0-only
//
// ローカル候補アドレスとランデブーサーバーのアドレスの選択。

#include "vocroz/network/address_selection.h"

#include <algorithm>
#include <array>

#include "vocroz/network/rendezvous_types.h"

namespace vocroz::network {

namespace {

// 候補の並び順。値の小さいものを先に置く。
enum class CandidateRank {
    kIpv6Global = 0,
    kIpv4 = 1,
    kIpv6UniqueLocal = 2,
    kExcluded = 3,
};

[[nodiscard]] bool is_all_zero(const Ipv6Bytes& ip) noexcept {
    return std::all_of(ip.begin(), ip.end(), [](std::uint8_t b) { return b == 0; });
}

[[nodiscard]] bool is_unspecified(const UdpAddress& address) noexcept {
    if (address.is_ipv4()) {
        const Ipv4Bytes v4 = address.ipv4();
        return v4[0] == 0 && v4[1] == 0 && v4[2] == 0 && v4[3] == 0;
    }
    return is_all_zero(address.ip);
}

[[nodiscard]] bool is_multicast(const UdpAddress& address) noexcept {
    if (address.is_ipv4()) {
        return (address.ipv4()[0] & 0xf0) == 0xe0;  // 224.0.0.0/4
    }
    return address.ip[0] == 0xff;  // ff00::/8
}

[[nodiscard]] CandidateRank rank_of(const UdpAddress& address) noexcept {
    if (is_unspecified(address) || is_multicast(address)) {
        return CandidateRank::kExcluded;
    }
    if (address.is_ipv4()) {
        const Ipv4Bytes v4 = address.ipv4();
        // 127.0.0.0/8（ループバック）と 169.254.0.0/16（リンクローカル）は相手から届かない。
        if (v4[0] == 127 || (v4[0] == 169 && v4[1] == 254)) {
            return CandidateRank::kExcluded;
        }
        return CandidateRank::kIpv4;
    }
    const Ipv6Bytes& ip = address.ip;
    // ::1（ループバック）。
    const bool loopback =
        std::all_of(ip.begin(), ip.end() - 1, [](std::uint8_t b) { return b == 0; }) && ip[15] == 1;
    // fe80::/10（リンクローカル）はスコープ ID がないと宛先にできない。
    const bool link_local = ip[0] == 0xfe && (ip[1] & 0xc0) == 0x80;
    // 2001::/32（Teredo）は中継を経由し、遅延が大きく到達性も不安定なので候補にしない。
    const bool teredo = ip[0] == 0x20 && ip[1] == 0x01 && ip[2] == 0 && ip[3] == 0;
    if (loopback || link_local || teredo) {
        return CandidateRank::kExcluded;
    }
    // fc00::/7（ユニークローカル）は同じ拠点の中でしか届かないので、IPv4 の後に置く。
    if ((ip[0] & 0xfe) == 0xfc) {
        return CandidateRank::kIpv6UniqueLocal;
    }
    return CandidateRank::kIpv6Global;
}

void append_unique(std::vector<UdpAddress>& out, const UdpAddress& address) {
    if (out.size() < kMaxLocalCandidates &&
        std::find(out.begin(), out.end(), address) == out.end()) {
        out.push_back(address);
    }
}

}  // namespace

std::vector<UdpAddress> select_local_candidates(std::span<const UdpAddress> interface_addresses,
                                                std::uint16_t local_port,
                                                const std::optional<UdpAddress>& mapped) {
    std::vector<UdpAddress> out;
    out.reserve(kMaxLocalCandidates);
    if (mapped && is_routable_candidate(*mapped)) {
        append_unique(out, *mapped);
    }
    constexpr std::array kOrder = {CandidateRank::kIpv6Global, CandidateRank::kIpv4,
                                   CandidateRank::kIpv6UniqueLocal};
    for (const CandidateRank rank : kOrder) {
        for (const UdpAddress& address : interface_addresses) {
            if (rank_of(address) == rank) {
                append_unique(out, address.with_port(local_port));
            }
        }
    }
    return out;
}

std::optional<UdpAddress> select_server_address(std::span<const UdpAddress> resolved) noexcept {
    const auto ipv4 = std::find_if(resolved.begin(), resolved.end(),
                                   [](const UdpAddress& a) { return a.is_ipv4(); });
    if (ipv4 != resolved.end()) {
        return *ipv4;
    }
    if (resolved.empty()) {
        return std::nullopt;
    }
    return resolved.front();
}

bool is_routable_candidate(const UdpAddress& address) noexcept {
    return address.port != 0 && !is_unspecified(address) && !is_multicast(address);
}

}  // namespace vocroz::network
