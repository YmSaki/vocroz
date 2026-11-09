// SPDX-License-Identifier: AGPL-3.0-only
//
// デフォルトゲートウェイの UDP 5351 番（PCP と NAT-PMP のサーバー）と 1 往復するポート。
// PCP と NAT-PMP の手順（PcpPortMapper）を、ソケットなしで単体テストできるようにするため、
// 送受信をこのポートに分ける。実装は Winsock（src/win/）。
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

#include "vocroz/common/result.h"
#include "vocroz/network/network_error.h"
#include "vocroz/network/udp_address.h"

namespace vocroz::network {

/// ゲートウェイの PCP / NAT-PMP サーバーとの送受信。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッドから使う。exchange() はブロックする。
class GatewayChannel {
public:
    virtual ~GatewayChannel() = default;

    /// ゲートウェイへ届くときの、こちらのローカルの IP アドレスを返す（ポートは 0）。
    /// PCP の要求に自分のアドレスを入れるのに使う。
    [[nodiscard]] virtual UdpAddress local_address() const noexcept = 0;

    /// 要求を 1 つ送り、応答を 1 つ待つ。ゲートウェイ以外から届いたデータグラムは捨てる。
    /// @param request  送る要求
    /// @param response 応答の書き込み先
    /// @param timeout  応答を待つ時間
    /// @return 応答の長さ。時間内に届かなければ kTimedOut、送れなければ kSendFailed
    [[nodiscard]] virtual common::Result<std::size_t, NetworkError> exchange(
        std::span<const std::uint8_t> request, std::span<std::uint8_t> response,
        std::chrono::milliseconds timeout) = 0;
};

}  // namespace vocroz::network
