// SPDX-License-Identifier: AGPL-3.0-only
//
// UDP の送受信先のアドレス（IP アドレスとポート）の値型。
// ソケットはデュアルスタック（docs/adr/0021-udp-socket-and-nat-traversal.md）なので、
// IPv4 のアドレスも IPv4 射影アドレス（::ffff:a.b.c.d）として 16 バイトで持ち、
// IPv4 と IPv6 を同じ型で比較と保持ができるようにする。
#pragma once

#include <array>
#include <cstdint>

namespace vocroz::network {

/// IPv6 アドレスのバイト列（ネットワークバイトオーダー）。
using Ipv6Bytes = std::array<std::uint8_t, 16>;

/// IPv4 アドレスのバイト列（ネットワークバイトオーダー）。
using Ipv4Bytes = std::array<std::uint8_t, 4>;

/// UDP のアドレス。IPv4 は IPv4 射影アドレスとして保持する。
/// IPv6 のスコープ ID は持たない。リンクローカルアドレスは候補に入れないため（docs/adr/0021）。
/// スレッド安全性: 通常の値型と同じ。
struct UdpAddress {
    /// IP アドレス。IPv4 なら ::ffff:a.b.c.d の形。
    Ipv6Bytes ip{};
    /// ポート番号（ホストバイトオーダー）。
    std::uint16_t port = 0;

    /// IPv4 のアドレスを作る。
    /// @param ipv4 IPv4 アドレス
    /// @param port ポート番号
    [[nodiscard]] static constexpr UdpAddress from_ipv4(const Ipv4Bytes& ipv4,
                                                        std::uint16_t port) noexcept {
        UdpAddress address;
        address.ip[10] = 0xff;
        address.ip[11] = 0xff;
        for (std::size_t i = 0; i < ipv4.size(); ++i) {
            address.ip[12 + i] = ipv4[i];
        }
        address.port = port;
        return address;
    }

    /// IPv6 のアドレスを作る。IPv4 射影アドレスを渡せば IPv4 のアドレスになる。
    /// @param ipv6 IPv6 アドレス
    /// @param port ポート番号
    [[nodiscard]] static constexpr UdpAddress from_ipv6(const Ipv6Bytes& ipv6,
                                                        std::uint16_t port) noexcept {
        UdpAddress address;
        address.ip = ipv6;
        address.port = port;
        return address;
    }

    /// IPv4 のアドレス（IPv4 射影アドレス）なら true を返す。
    [[nodiscard]] constexpr bool is_ipv4() const noexcept {
        for (std::size_t i = 0; i < 10; ++i) {
            if (ip[i] != 0) {
                return false;
            }
        }
        return ip[10] == 0xff && ip[11] == 0xff;
    }

    /// IPv4 のアドレスを返す。is_ipv4() が false のときは意味のない値になる。
    [[nodiscard]] constexpr Ipv4Bytes ipv4() const noexcept {
        return {ip[12], ip[13], ip[14], ip[15]};
    }

    /// 同じ IP アドレスでポートだけを変えたアドレスを返す。
    /// @param new_port 新しいポート番号
    [[nodiscard]] constexpr UdpAddress with_port(std::uint16_t new_port) const noexcept {
        UdpAddress address = *this;
        address.port = new_port;
        return address;
    }

    friend constexpr bool operator==(const UdpAddress&, const UdpAddress&) = default;
};

}  // namespace vocroz::network
