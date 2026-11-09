// SPDX-License-Identifier: AGPL-3.0-only
//
// network の名前解決（NetworkEnvironment）と UDP ソケット（UdpEndpoint）で、SRTP 外部出力の
// 宛先ごとのソケットを開く ExternalSocketOpener（docs/adr/0040-srtp-external-output.md）。
// ソケットは宛先ごとに新しく開き、OS が選ぶポートにバインドする。受信したデータグラム
// （受信機が返す SRTCP など）は今は使わないので捨てる。
#pragma once

#include <functional>
#include <memory>

#include "vocroz/common/result.h"
#include "vocroz/network/network_error.h"
#include "vocroz/network/ports/datagram_listener.h"
#include "vocroz/network/ports/network_environment.h"
#include "vocroz/network/ports/udp_endpoint.h"

#include "external_socket_opener.h"

namespace vocroz::engine {

/// network の部品で外部出力のソケットを開く。
/// スレッド安全性: コントロールスレッドから使う。
class NetworkExternalSocketOpener final : public ExternalSocketOpener {
public:
    /// UDP ソケットを開く関数。受信したデータグラムは渡したリスナーへ届く。
    /// 本番は network::make_winsock_udp_endpoint() に OS が選ぶポート（0）を渡す。
    using EndpointFactory =
        std::function<common::Result<std::unique_ptr<network::UdpEndpoint>, network::NetworkError>(
            network::DatagramListener&)>;

    /// @param environment 名前解決。このオブジェクトより長く生存させる
    /// @param open_endpoint UDP ソケットを開く関数
    NetworkExternalSocketOpener(const network::NetworkEnvironment& environment,
                                EndpointFactory open_endpoint);

    [[nodiscard]] common::Result<std::unique_ptr<transport::DatagramSocket>,
                                 control_protocol::ExternalOutputResult>
    open(std::string_view host, std::uint16_t port) override;

private:
    const network::NetworkEnvironment& environment_;
    EndpointFactory open_endpoint_;
};

}  // namespace vocroz::engine
