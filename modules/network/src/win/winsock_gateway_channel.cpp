// SPDX-License-Identifier: AGPL-3.0-only
//
// Winsock による GatewayChannel。

#include "win/winsock_gateway_channel.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include "network_constants.h"
#include "win/win_network_environment.h"
#include "win/winsock_address.h"

namespace vocroz::network::win {

common::Result<std::unique_ptr<GatewayChannel>, NetworkError> WinsockGatewayChannel::open() {
    std::unique_ptr<WinsockGatewayChannel> channel(new WinsockGatewayChannel());
    if (!channel->runtime_.ok()) {
        return common::make_unexpected(NetworkError::kSocketUnavailable);
    }
    const auto gateway = find_default_ipv4_gateway();
    if (!gateway) {
        return common::make_unexpected(NetworkError::kNoGateway);
    }
    channel->socket_ = SocketHandle(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (!channel->socket_.valid()) {
        return common::make_unexpected(NetworkError::kSocketUnavailable);
    }
    const auto destination = to_sockaddr_in(gateway->with_port(kPcpServerPort));
    if (connect(channel->socket_.get(), reinterpret_cast<const sockaddr*>(&*destination),
                sizeof(*destination)) != 0) {
        return common::make_unexpected(NetworkError::kNoGateway);
    }
    // connect() した後のローカルアドレスが、ゲートウェイへ届くときのこちらのアドレスになる。
    sockaddr_storage local{};
    int local_length = sizeof(local);
    if (getsockname(channel->socket_.get(), reinterpret_cast<sockaddr*>(&local), &local_length) !=
        0) {
        return common::make_unexpected(NetworkError::kSocketUnavailable);
    }
    const auto local_address = from_sockaddr(reinterpret_cast<const sockaddr*>(&local));
    if (!local_address) {
        return common::make_unexpected(NetworkError::kSocketUnavailable);
    }
    channel->local_ = local_address->with_port(0);
    return std::unique_ptr<GatewayChannel>(std::move(channel));
}

common::Result<std::size_t, NetworkError> WinsockGatewayChannel::exchange(
    std::span<const std::uint8_t> request, std::span<std::uint8_t> response,
    std::chrono::milliseconds timeout) {
    const int length = static_cast<int>(request.size());
    if (send(socket_.get(), reinterpret_cast<const char*>(request.data()), length, 0) != length) {
        return common::make_unexpected(NetworkError::kSendFailed);
    }
    WSAPOLLFD poll_fd{};
    poll_fd.fd = socket_.get();
    poll_fd.events = POLLRDNORM;
    const int ready = WSAPoll(&poll_fd, 1, static_cast<INT>(timeout.count()));
    if (ready == 0) {
        return common::make_unexpected(NetworkError::kTimedOut);
    }
    if (ready < 0) {
        return common::make_unexpected(NetworkError::kSocketUnavailable);
    }
    const int received = recv(socket_.get(), reinterpret_cast<char*>(response.data()),
                              static_cast<int>(response.size()), 0);
    if (received == SOCKET_ERROR) {
        // ゲートウェイが 5351 番を閉じている（PCP にも NAT-PMP にも対応していない）。
        if (WSAGetLastError() == WSAECONNRESET) {
            return common::make_unexpected(NetworkError::kPortMappingUnsupported);
        }
        return common::make_unexpected(NetworkError::kTimedOut);
    }
    return static_cast<std::size_t>(received);
}

}  // namespace vocroz::network::win
