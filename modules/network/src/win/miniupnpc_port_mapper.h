// SPDX-License-Identifier: AGPL-3.0-only
//
// miniupnpc による UPnP-IGD のポート転送（docs/adr/0021-udp-socket-and-nat-traversal.md）。
// SSDP でルーターを探し、WANIPConnection の AddPortMapping で UDP の転送を設定する。
// miniupnpc は Windows のビルドでだけ取得するので、このアダプターも src/win/ に置く。
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "vocroz/network/ports/port_mapper.h"

#include "win/winsock_runtime.h"

namespace vocroz::network::win {

/// UPnP-IGD の PortMapper。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッドから使う。map() と unmap() はブロックする
/// （探索に最大 2 秒、SOAP の呼び出しにルーターの応答時間）。
class MiniupnpcPortMapper final : public PortMapper {
public:
    MiniupnpcPortMapper();
    ~MiniupnpcPortMapper() override;
    MiniupnpcPortMapper(const MiniupnpcPortMapper&) = delete;
    MiniupnpcPortMapper& operator=(const MiniupnpcPortMapper&) = delete;

    /// ルーターが見つからない、接続されていない、または外部アドレスがプライベート（二重 NAT）なら
    /// kPortMappingUnsupported、AddPortMapping が失敗したら kPortMappingRefused を返す。
    [[nodiscard]] common::Result<PortMapping, NetworkError> map(
        std::uint16_t internal_port, std::chrono::seconds lifetime) override;

    void unmap() override;

private:
    // 探索で見つけたルーター。miniupnpc の構造体は公開ヘッダーに出さないため、.cpp で定義する。
    struct Gateway;

    [[nodiscard]] common::Result<void, NetworkError> discover();

    WinsockRuntime runtime_;
    std::unique_ptr<Gateway> gateway_;
    // 割り当て済みの外部ポート。更新と解除に使う。
    std::optional<std::uint16_t> mapped_external_port_;
};

}  // namespace vocroz::network::win
