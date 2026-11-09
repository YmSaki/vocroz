// SPDX-License-Identifier: AGPL-3.0-only
//
// ローカルのポートにバインドした UDP ソケットのポート。実装は Winsock（src/win/）。
// ランデブーサーバーへの登録、ホールパンチ、相手とのメディアと制御を 1 本のソケットで送受信する
// （docs/adr/0021-udp-socket-and-nat-traversal.md）。ランデブーサーバーが見た外部アドレスと、
// 相手と通信するときの外部アドレスを一致させるため。
#pragma once

#include <cstdint>
#include <span>

#include "vocroz/common/result.h"
#include "vocroz/network/network_error.h"
#include "vocroz/network/udp_address.h"

namespace vocroz::network {

/// 宛先を毎回指定して送る UDP ソケット。受信は生成時に渡した DatagramListener に届く。
class UdpEndpoint {
public:
    virtual ~UdpEndpoint() = default;

    /// データグラムを 1 つ送る。ブロックしない。届くことは保証しない。
    /// @param to       宛先
    /// @param datagram UDP のペイロード（空でもよい）。呼び出しから戻った後は参照しない
    /// @return 送れなければ kSendFailed、IPv4 だけのソケットで IPv6 に送ろうとしたら
    ///         kAddressFamilyUnsupported
    /// スレッド安全性: どのスレッドからも同時に呼び出し可能。
    [[nodiscard]] virtual common::Result<void, NetworkError> send_to(
        const UdpAddress& to, std::span<const std::uint8_t> datagram) noexcept = 0;

    /// バインドしたローカルのポートを返す。
    /// スレッド安全性: どのスレッドからも呼び出し可能。
    [[nodiscard]] virtual std::uint16_t local_port() const noexcept = 0;
};

}  // namespace vocroz::network
