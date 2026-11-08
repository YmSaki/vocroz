// SPDX-License-Identifier: AGPL-3.0-only
//
// モジュール内の複数のポート開放の実装（PCP、NAT-PMP、UPnP-IGD）が使う、アドレスの判定。
#pragma once

#include "vocroz/network/udp_address.h"

namespace vocroz::network::detail {

/// インターネットから直接届かない IPv4 アドレス（プライベート、CGNAT の共有アドレス）なら true
/// を返す。 ゲートウェイの外部アドレスがこれに当たるときは、ゲートウェイの外にもう 1 段 NAT
/// があり（二重 NAT）、 ポート開放をしても外から届かないので、開放は成功として扱わない。
/// @param address 調べるアドレス。IPv6 なら false
[[nodiscard]] constexpr bool is_non_public_ipv4(const UdpAddress& address) noexcept {
    if (!address.is_ipv4()) {
        return false;
    }
    const Ipv4Bytes v4 = address.ipv4();
    const bool rfc1918 =
        v4[0] == 10 || (v4[0] == 172 && (v4[1] & 0xf0) == 16) || (v4[0] == 192 && v4[1] == 168);
    const bool shared = v4[0] == 100 && (v4[1] & 0xc0) == 64;  // 100.64.0.0/10（CGNAT）
    const bool unspecified = v4[0] == 0;
    return rfc1918 || shared || unspecified;
}

}  // namespace vocroz::network::detail
