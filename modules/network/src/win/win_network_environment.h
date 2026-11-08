// SPDX-License-Identifier: AGPL-3.0-only
//
// Win32 の API による NetworkEnvironment と、デフォルトゲートウェイの検出。
// 名前解決は getaddrinfo、インターフェースのアドレスとゲートウェイは GetAdaptersAddresses
// で取得する。
#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "vocroz/network/ports/network_environment.h"

#include "win/winsock_runtime.h"

namespace vocroz::network::win {

/// Win32 の NetworkEnvironment。
/// スレッド安全性: どのスレッドからも同時に呼び出し可能。resolve() はブロックする。
class WinNetworkEnvironment final : public NetworkEnvironment {
public:
    [[nodiscard]] common::Result<std::vector<UdpAddress>, NetworkError> resolve(
        std::string_view host, std::uint16_t port) const override;

    [[nodiscard]] std::vector<UdpAddress> interface_addresses() const override;

private:
    WinsockRuntime runtime_;
};

/// IPv4 のデフォルトゲートウェイのアドレスを返す（ポートは 0）。
/// 動作中のアダプターのうち、IPv4 のメトリックが最も小さいもののゲートウェイを選ぶ。
/// OS が通常の通信に使う経路のゲートウェイと一致させ、そこでポートを開放するため。
/// @return ゲートウェイ。見つからなければ std::nullopt
[[nodiscard]] std::optional<UdpAddress> find_default_ipv4_gateway();

}  // namespace vocroz::network::win
