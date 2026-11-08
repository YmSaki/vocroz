// SPDX-License-Identifier: AGPL-3.0-only
//
// PCP（RFC 6887）と NAT-PMP（RFC 6886）によるポート転送の設定。
// PCP を先に送り、ゲートウェイが NAT-PMP のバージョンで応答したら NAT-PMP に切り替える
// （docs/adr/0021-udp-socket-and-nat-traversal.md）。どちらもゲートウェイの UDP 5351 番への 1
// 往復なので、 送受信は GatewayChannel に任せ、ここでは手順だけを持つ。
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "vocroz/common/result.h"
#include "vocroz/network/network_error.h"
#include "vocroz/network/ports/gateway_channel.h"
#include "vocroz/network/ports/port_mapper.h"
#include "vocroz/transport/ports/random_source.h"

#include "wire/pcp_codec.h"

namespace vocroz::network {

/// PCP と NAT-PMP の PortMapper。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッドから使う。map() と unmap() はブロックする。
class PcpPortMapper final : public PortMapper {
public:
    /// ゲートウェイとの送受信を開く関数。map()
    /// のたびに呼ぶので、回線が変わっても新しいゲートウェイを使う。
    using ChannelOpener =
        std::function<common::Result<std::unique_ptr<GatewayChannel>, NetworkError>()>;

    /// @param open_channel ゲートウェイとの送受信を開く関数
    /// @param random       PCP の nonce に使う乱数。PcpPortMapper より長く生きること
    PcpPortMapper(ChannelOpener open_channel, transport::RandomSource& random);

    /// PCP、だめなら NAT-PMP で転送を割り当てる。
    /// 外部アドレスがプライベートアドレス（二重 NAT）なら外から届かないので kPortMappingUnsupported
    /// を返す。
    [[nodiscard]] common::Result<PortMapping, NetworkError> map(
        std::uint16_t internal_port, std::chrono::seconds lifetime) override;

    /// 割り当てた転送を、寿命 0 の要求で解除する。応答は待つが、結果は確かめない。
    void unmap() override;

private:
    // 割り当て済みの転送。更新と解除に同じ値を使う。
    struct ActiveMapping {
        PortMappingMethod method;
        std::uint16_t internal_port;
        std::uint16_t external_port;
    };

    [[nodiscard]] common::Result<PortMapping, NetworkError> map_with_pcp(
        GatewayChannel& channel, std::uint16_t internal_port, std::chrono::seconds lifetime);
    [[nodiscard]] common::Result<PortMapping, NetworkError> map_with_nat_pmp(
        GatewayChannel& channel, std::uint16_t internal_port, std::chrono::seconds lifetime);

    ChannelOpener open_channel_;
    transport::RandomSource& random_;
    // PCP は更新と削除に、作成時と同じ nonce を使う必要がある。
    std::optional<wire::PcpNonce> nonce_;
    // NAT-PMP にしか対応していないと分かったゲートウェイには、次から PCP を送らない。
    bool nat_pmp_only_ = false;
    std::optional<ActiveMapping> active_;
};

}  // namespace vocroz::network
