// SPDX-License-Identifier: AGPL-3.0-only
//
// network のアダプターのファクトリー関数。

#include "vocroz/network/factories.h"

#include <utility>
#include <vector>

#include "chained_port_mapper.h"
#include "pcp_port_mapper.h"
#include "win/miniupnpc_port_mapper.h"
#include "win/win_network_environment.h"
#include "win/winsock_gateway_channel.h"
#include "win/winsock_udp_endpoint.h"

namespace vocroz::network {

common::Result<std::unique_ptr<UdpEndpoint>, NetworkError> make_winsock_udp_endpoint(
    std::uint16_t port, DatagramListener& listener) {
    auto endpoint = win::WinsockUdpEndpoint::open(port, listener);
    if (!endpoint) {
        return common::make_unexpected(endpoint.error());
    }
    return std::unique_ptr<UdpEndpoint>(std::move(endpoint).value());
}

std::unique_ptr<NetworkEnvironment> make_win_network_environment() {
    return std::make_unique<win::WinNetworkEnvironment>();
}

std::unique_ptr<PortMapper> make_port_mapper(transport::RandomSource& random) {
    std::vector<std::unique_ptr<PortMapper>> mappers;
    mappers.push_back(std::make_unique<PcpPortMapper>(&win::WinsockGatewayChannel::open, random));
    mappers.push_back(std::make_unique<win::MiniupnpcPortMapper>());
    return std::make_unique<ChainedPortMapper>(std::move(mappers));
}

}  // namespace vocroz::network
