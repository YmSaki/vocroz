// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の NetworkEnvironment。名前解決は決まったアドレスを返し、ローカルのアドレスは
// テストが設定した値を返す。
#pragma once

#include <string_view>
#include <vector>

#include "vocroz/network/ports/network_environment.h"

namespace vocroz::client {

/// 決まった値を返す NetworkEnvironment。
class FakeNetworkEnvironment final : public network::NetworkEnvironment {
public:
    /// resolved を返す。fail_resolve が true なら kResolveFailed。
    [[nodiscard]] common::Result<std::vector<network::UdpAddress>, network::NetworkError> resolve(
        std::string_view /*host*/, std::uint16_t /*port*/) const override {
        if (fail_resolve) {
            return common::make_unexpected(network::NetworkError::kResolveFailed);
        }
        return resolved;
    }

    [[nodiscard]] std::vector<network::UdpAddress> interface_addresses() const override {
        return interfaces;
    }

    /// resolve() が返すアドレス。
    std::vector<network::UdpAddress> resolved;
    /// interface_addresses() が返すアドレス。
    std::vector<network::UdpAddress> interfaces;
    /// true の間は名前解決を失敗させる。
    bool fail_resolve = false;
};

}  // namespace vocroz::client
