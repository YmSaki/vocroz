// SPDX-License-Identifier: AGPL-3.0-only
//
// 複数のポート開放の方法を順に試す PortMapper。PCP と NAT-PMP（PcpPortMapper）、UPnP-IGD
// の順に試し、
// 最初に成功した方法で更新と解除を続ける（docs/adr/0021-udp-socket-and-nat-traversal.md）。
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "vocroz/network/ports/port_mapper.h"

namespace vocroz::network {

/// 順に試す PortMapper。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッドから使う。map() と unmap() はブロックする。
class ChainedPortMapper final : public PortMapper {
public:
    /// @param mappers 試す順の PortMapper
    explicit ChainedPortMapper(std::vector<std::unique_ptr<PortMapper>> mappers);

    /// 前回成功した方法があればそれで更新し、だめなら先頭から順に試す。
    /// @return 最初に成功した割り当て。すべて失敗したら最後の失敗の原因
    [[nodiscard]] common::Result<PortMapping, NetworkError> map(
        std::uint16_t internal_port, std::chrono::seconds lifetime) override;

    /// 成功した方法で割り当てを解除する。
    void unmap() override;

private:
    std::vector<std::unique_ptr<PortMapper>> mappers_;
    std::optional<std::size_t> active_;
};

}  // namespace vocroz::network
