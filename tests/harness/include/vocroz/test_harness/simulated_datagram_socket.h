// SPDX-License-Identifier: AGPL-3.0-only
//
// transport の DatagramSocket ポートを SimulatedNetwork で実装するアダプター。
// 製品では network モジュールの Winsock の実装が入る位置に差し込み、transport と、
// これから入る session をそのままシミュレーションのネットワークにつなぐ。
#pragma once

#include <cstdint>
#include <span>

#include "vocroz/common/result.h"
#include "vocroz/netsim/simulated_network.h"
#include "vocroz/transport/ports/datagram_socket.h"
#include "vocroz/transport/transport_error.h"

namespace vocroz::test_harness {

/// local から remote へ送るソケット。
/// スレッド安全性: SimulatedNetwork と同じスレッドから使う。
class SimulatedDatagramSocket final : public transport::DatagramSocket {
public:
    /// @param network 送り先のネットワーク。このオブジェクトより長く生存させる
    /// @param local   自分のアドレス
    /// @param remote  相手のアドレス
    SimulatedDatagramSocket(netsim::SimulatedNetwork& network, const netsim::NodeAddress& local,
                            const netsim::NodeAddress& remote) noexcept
        : network_(network), local_(local), remote_(remote) {}

    /// データグラムをネットワークに渡す。シミュレーションでは送信は失敗しない。
    [[nodiscard]] common::Result<void, transport::TransportError> send(
        std::span<const std::uint8_t> datagram) noexcept override;

private:
    netsim::SimulatedNetwork& network_;
    netsim::NodeAddress local_;
    netsim::NodeAddress remote_;
};

}  // namespace vocroz::test_harness
