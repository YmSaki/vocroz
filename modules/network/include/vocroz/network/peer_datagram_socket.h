// SPDX-License-Identifier: AGPL-3.0-only
//
// 1 つの相手へ送る transport::DatagramSocket の実装。UdpEndpoint の上に送信先を 1 つ固定して送る。
// 相手の送信元が変わったとき（NAT のマッピングの変化、より良い経路の確立）は、認証済みのパケットを
// 受け取った受信スレッドが update_remote() で新しい送信先を渡す。送信先は送信スレッドだけが持ち、
// 受信スレッドからは SPSC キューで渡す（AGENTS.md 6.1）。
#pragma once

#include <cstdint>
#include <span>

#include "vocroz/common/spsc_queue.h"
#include "vocroz/network/ports/udp_endpoint.h"
#include "vocroz/network/udp_address.h"
#include "vocroz/transport/ports/datagram_socket.h"

namespace vocroz::network {

/// 送信先が 1 つの DatagramSocket。
class PeerDatagramSocket final : public transport::DatagramSocket {
public:
    /// @param endpoint 送信に使うソケット。PeerDatagramSocket より長く生きること
    /// @param remote   最初の送信先
    PeerDatagramSocket(UdpEndpoint& endpoint, const UdpAddress& remote) noexcept;

    /// 現在の送信先へデータグラムを送る。送る前に update_remote() で渡された送信先を反映する。
    /// @return 送れなければ kSendFailed
    /// スレッド安全性: 1 つの送信スレッドから呼ぶ。ブロックせず、ヒープを確保しない。
    [[nodiscard]] common::Result<void, transport::TransportError> send(
        std::span<const std::uint8_t> datagram) noexcept override;

    /// 送信先を変える。次の send() から反映する。
    /// @param remote 新しい送信先（認証済みのパケットの送信元）
    /// @return 渡せたら true。未反映の更新がたまっていて渡せなければ
    /// false（次の認証済みパケットで再び渡す） スレッド安全性: 1
    /// つのスレッド（ネットワーク受信スレッド）から呼ぶ。ブロックせず、ヒープを確保しない。
    [[nodiscard]] bool update_remote(const UdpAddress& remote) noexcept;

private:
    // 経路の切り替えはまれなので、未反映の更新は数個たまれば足りる。
    static constexpr std::size_t kUpdateQueueCapacity = 8;

    UdpEndpoint& endpoint_;
    UdpAddress remote_;  // 送信スレッドだけが読み書きする
    common::SpscQueue<UdpAddress, kUpdateQueueCapacity> updates_;
};

}  // namespace vocroz::network
