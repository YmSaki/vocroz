// SPDX-License-Identifier: AGPL-3.0-only
//
// UdpAddress と Winsock のソケットアドレスの変換。

#include "win/winsock_address.h"

#include <cstring>

namespace vocroz::network::win {

std::optional<UdpAddress> from_sockaddr(const sockaddr* address) noexcept {
    if (address == nullptr) {
        return std::nullopt;
    }
    if (address->sa_family == AF_INET) {
        sockaddr_in v4{};
        std::memcpy(&v4, address, sizeof(v4));
        Ipv4Bytes ip{};
        std::memcpy(ip.data(), &v4.sin_addr, ip.size());
        return UdpAddress::from_ipv4(ip, ntohs(v4.sin_port));
    }
    if (address->sa_family == AF_INET6) {
        sockaddr_in6 v6{};
        std::memcpy(&v6, address, sizeof(v6));
        Ipv6Bytes ip{};
        std::memcpy(ip.data(), &v6.sin6_addr, ip.size());
        return UdpAddress::from_ipv6(ip, ntohs(v6.sin6_port));
    }
    return std::nullopt;
}

sockaddr_in6 to_sockaddr_in6(const UdpAddress& address) noexcept {
    sockaddr_in6 out{};
    out.sin6_family = AF_INET6;
    out.sin6_port = htons(address.port);
    std::memcpy(&out.sin6_addr, address.ip.data(), address.ip.size());
    return out;
}

std::optional<sockaddr_in> to_sockaddr_in(const UdpAddress& address) noexcept {
    if (!address.is_ipv4()) {
        return std::nullopt;
    }
    sockaddr_in out{};
    out.sin_family = AF_INET;
    out.sin_port = htons(address.port);
    const Ipv4Bytes ip = address.ipv4();
    std::memcpy(&out.sin_addr, ip.data(), ip.size());
    return out;
}

}  // namespace vocroz::network::win
