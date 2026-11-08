// SPDX-License-Identifier: AGPL-3.0-only
//
// UdpAddress と Winsock のソケットアドレス（sockaddr_in、sockaddr_in6）の変換。
// 同じ変換を UDP ソケット、ゲートウェイとの通信、名前解決、インターフェースの列挙で使う。
#pragma once

#include <optional>

#include <winsock2.h>
#include <ws2tcpip.h>

#include "vocroz/network/udp_address.h"

namespace vocroz::network::win {

/// ソケットアドレスを UdpAddress に変換する。IPv4 は IPv4 射影アドレスにする。
/// @param address ソケットアドレス（AF_INET か AF_INET6）
/// @return 変換したアドレス。それ以外のファミリーなら std::nullopt
[[nodiscard]] std::optional<UdpAddress> from_sockaddr(const sockaddr* address) noexcept;

/// UdpAddress を AF_INET6 のソケットアドレスに変換する（デュアルスタックのソケットで送るとき）。
/// @param address 変換するアドレス
[[nodiscard]] sockaddr_in6 to_sockaddr_in6(const UdpAddress& address) noexcept;

/// UdpAddress を AF_INET のソケットアドレスに変換する（IPv4 だけのソケットで送るとき）。
/// @param address 変換するアドレス
/// @return 変換したアドレス。IPv4 でなければ std::nullopt
[[nodiscard]] std::optional<sockaddr_in> to_sockaddr_in(const UdpAddress& address) noexcept;

}  // namespace vocroz::network::win
