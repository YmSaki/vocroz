// SPDX-License-Identifier: AGPL-3.0-only
//
// OS のネットワークの情報（名前解決、ローカルのアドレス）を取得するポート。実装は
// Win32（src/win/）。 候補アドレスの選び方（address_selection.h）を、OS
// なしで単体テストできるよう、取得と選択を分ける。
#pragma once

#include <string_view>
#include <vector>

#include "vocroz/common/result.h"
#include "vocroz/network/network_error.h"
#include "vocroz/network/udp_address.h"

namespace vocroz::network {

/// OS のネットワークの情報。
/// スレッド安全性: どのスレッドからも呼び出し可能。呼び出しはブロックしうる（名前解決）ので、
/// オーディオスレッドとネットワーク受信スレッドからは呼ばない。
class NetworkEnvironment {
public:
    virtual ~NetworkEnvironment() = default;

    /// ホスト名を UDP のアドレスに解決する。
    /// @param host ホスト名か IP アドレスの文字列
    /// @param port ポート番号
    /// @return 解決したアドレス（OS が返した順）。解決できなければ kResolveFailed
    [[nodiscard]] virtual common::Result<std::vector<UdpAddress>, NetworkError> resolve(
        std::string_view host, std::uint16_t port) const = 0;

    /// 動作中のネットワークインターフェースのユニキャストアドレスを返す（ポートは 0）。
    /// 候補として使えるかどうかは select_local_candidates() で選ぶ。
    [[nodiscard]] virtual std::vector<UdpAddress> interface_addresses() const = 0;
};

}  // namespace vocroz::network
