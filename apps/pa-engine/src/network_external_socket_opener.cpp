// SPDX-License-Identifier: AGPL-3.0-only
//
// network_external_socket_opener.h の実装。

#include "network_external_socket_opener.h"

#include <span>
#include <utility>

#include "vocroz/network/udp_address.h"

namespace vocroz::engine {
namespace {

namespace cp = control_protocol;

// 受信したデータグラムを捨てるリスナー。
class DiscardingListener final : public network::DatagramListener {
public:
    void on_datagram(const network::UdpAddress& /*from*/,
                     std::span<const std::uint8_t> /*datagram*/) noexcept override {}
};

// 宛先を 1 つに固定したソケット。受信のリスナーとソケットを一緒に持ち、破棄の順序をそろえる
// （ソケットの受信スレッドが止まってからリスナーを破棄する）。
class ExternalDestinationSocket final : public transport::DatagramSocket {
public:
    ExternalDestinationSocket(std::unique_ptr<DiscardingListener> listener,
                              std::unique_ptr<network::UdpEndpoint> endpoint,
                              const network::UdpAddress& remote) noexcept
        : listener_(std::move(listener)), endpoint_(std::move(endpoint)), remote_(remote) {}

    [[nodiscard]] common::Result<void, transport::TransportError> send(
        std::span<const std::uint8_t> datagram) noexcept override {
        if (!endpoint_->send_to(remote_, datagram)) {
            return common::make_unexpected(transport::TransportError::kSendFailed);
        }
        return {};
    }

private:
    std::unique_ptr<DiscardingListener> listener_;
    std::unique_ptr<network::UdpEndpoint> endpoint_;
    network::UdpAddress remote_;
};

}  // namespace

NetworkExternalSocketOpener::NetworkExternalSocketOpener(
    const network::NetworkEnvironment& environment, EndpointFactory open_endpoint)
    : environment_(environment), open_endpoint_(std::move(open_endpoint)) {}

common::Result<std::unique_ptr<transport::DatagramSocket>, cp::ExternalOutputResult>
NetworkExternalSocketOpener::open(std::string_view host, std::uint16_t port) {
    const auto resolved = environment_.resolve(host, port);
    if (!resolved || resolved.value().empty()) {
        return common::make_unexpected(cp::ExternalOutputResult::kResolveFailed);
    }
    // 名前解決が返した最初のアドレスへ送る。OS は使える経路のアドレスを先に返すので、
    // 受信機の設定と同じ順の選び方になる。
    const network::UdpAddress remote = resolved.value().front();
    auto listener = std::make_unique<DiscardingListener>();
    auto endpoint = open_endpoint_(*listener);
    if (!endpoint) {
        return common::make_unexpected(cp::ExternalOutputResult::kSendFailed);
    }
    return std::unique_ptr<transport::DatagramSocket>(std::make_unique<ExternalDestinationSocket>(
        std::move(listener), std::move(endpoint).value(), remote));
}

}  // namespace vocroz::engine
