// SPDX-License-Identifier: AGPL-3.0-only
//
// Winsock による GatewayChannel。デフォルトゲートウェイの UDP 5351 番へ connect() した IPv4 の
// ソケットで、PCP と NAT-PMP の要求を送り、応答を待つ。connect() したソケットは、
// ゲートウェイ以外から届いたデータグラムを OS が捨てるので、なりすました応答を受け取らない。
#pragma once

#include <memory>

#include "vocroz/common/result.h"
#include "vocroz/network/ports/gateway_channel.h"

#include "win/socket_handle.h"
#include "win/winsock_runtime.h"

namespace vocroz::network::win {

/// ゲートウェイの PCP / NAT-PMP サーバーとの送受信。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッドから使う。
class WinsockGatewayChannel final : public GatewayChannel {
public:
    /// デフォルトゲートウェイを探し、そこへ connect() したソケットを開く。
    /// @return 開いたチャンネル。ゲートウェイがなければ kNoGateway、ソケットを作れなければ
    /// kSocketUnavailable
    [[nodiscard]] static common::Result<std::unique_ptr<GatewayChannel>, NetworkError> open();

    [[nodiscard]] UdpAddress local_address() const noexcept override {
        return local_;
    }

    /// ゲートウェイがポートを閉じていて ICMP の到達不能が返ったら、待たずに kPortMappingUnsupported
    /// を返す。
    [[nodiscard]] common::Result<std::size_t, NetworkError> exchange(
        std::span<const std::uint8_t> request, std::span<std::uint8_t> response,
        std::chrono::milliseconds timeout) override;

private:
    WinsockGatewayChannel() = default;

    WinsockRuntime runtime_;
    SocketHandle socket_;
    UdpAddress local_;
};

}  // namespace vocroz::network::win
