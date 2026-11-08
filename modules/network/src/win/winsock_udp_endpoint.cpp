// SPDX-License-Identifier: AGPL-3.0-only
//
// Winsock による UdpEndpoint。

#include "win/winsock_udp_endpoint.h"

#include <array>

#include <windows.h>
#include <ws2tcpip.h>

#include "vocroz/transport/constants.h"

#include "win/winsock_address.h"

namespace vocroz::network::win {

namespace {

// 受信と送信のソケットバッファ（バイト）。受信スレッドが一時的に遅れても、5ms フレームで
// 数十の相手から届く分（1 秒あたり数百 KB）を落とさずにためられる大きさにする。
constexpr int kSocketBufferBytes = 1 << 20;

// 受信バッファ。transport
// のパケットの最大長より大きくし、長すぎるデータグラムを切り詰めずに見分ける。
constexpr std::size_t kReceiveBufferBytes = 2048;
static_assert(kReceiveBufferBytes > transport::kMaxPacketBytes);

[[nodiscard]] bool set_int_option(SOCKET socket, int level, int name, int value) noexcept {
    return setsockopt(socket, level, name, reinterpret_cast<const char*>(&value), sizeof(value)) ==
           0;
}

// SIO_UDP_CONNRESET の値。MSVC では mstcpip.h にあるが、MinGW
// のヘッダーにはないので、同じ定義をここに持つ。
const DWORD kSioUdpConnReset = _WSAIOW(IOC_VENDOR, 12);

// 相手のポートが閉じていると届く ICMP の到達不能で、以降の recvfrom() が WSAECONNRESET で
// 失敗し続けないようにする。ホールパンチでは、まだ開いていない相手のポートへ送るのが普通なので必要になる。
void disable_connection_reset(SOCKET socket) noexcept {
    BOOL enable = FALSE;
    DWORD returned = 0;
    WSAIoctl(socket, kSioUdpConnReset, &enable, sizeof(enable), nullptr, 0, &returned, nullptr,
             nullptr);
}

}  // namespace

common::Result<std::unique_ptr<WinsockUdpEndpoint>, NetworkError> WinsockUdpEndpoint::open(
    std::uint16_t port, DatagramListener& listener) {
    std::unique_ptr<WinsockUdpEndpoint> endpoint(new WinsockUdpEndpoint(listener));
    if (!endpoint->runtime_.ok() || !endpoint->readable_.valid() || !endpoint->stop_.valid()) {
        return common::make_unexpected(NetworkError::kSocketUnavailable);
    }
    if (auto bound = endpoint->bind_socket(port); !bound) {
        return common::make_unexpected(bound.error());
    }
    // WSAEventSelect はソケットを非ブロッキングにする。送信がブロックしないのもこのため。
    if (WSAEventSelect(endpoint->socket_.get(), endpoint->readable_.get(), FD_READ) != 0) {
        return common::make_unexpected(NetworkError::kSocketUnavailable);
    }
    endpoint->receiver_ = std::thread([raw = endpoint.get()] { raw->receive_loop(); });
    return endpoint;
}

WinsockUdpEndpoint::WinsockUdpEndpoint(DatagramListener& listener) noexcept : listener_(listener) {}

WinsockUdpEndpoint::~WinsockUdpEndpoint() {
    if (receiver_.joinable()) {
        WSASetEvent(stop_.get());
        receiver_.join();
    }
}

common::Result<void, NetworkError> WinsockUdpEndpoint::bind_socket(std::uint16_t port) {
    socket_ = SocketHandle(socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP));
    if (socket_.valid()) {
        // 0 にすると IPv4 の相手とも IPv4 射影アドレスで通信できる（Windows の既定は 1）。
        if (!set_int_option(socket_.get(), IPPROTO_IPV6, IPV6_V6ONLY, 0)) {
            socket_.reset();
        }
    }
    dual_stack_ = socket_.valid();
    if (!dual_stack_) {
        socket_ = SocketHandle(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
        if (!socket_.valid()) {
            return common::make_unexpected(NetworkError::kSocketUnavailable);
        }
    }
    // ほかのプロセスが同じポートにバインドして、こちら宛てのパケットを横取りできないようにする。
    (void)set_int_option(socket_.get(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
    (void)set_int_option(socket_.get(), SOL_SOCKET, SO_RCVBUF, kSocketBufferBytes);
    (void)set_int_option(socket_.get(), SOL_SOCKET, SO_SNDBUF, kSocketBufferBytes);
    disable_connection_reset(socket_.get());

    int bound = SOCKET_ERROR;
    if (dual_stack_) {
        sockaddr_in6 any{};
        any.sin6_family = AF_INET6;
        any.sin6_addr = in6addr_any;
        any.sin6_port = htons(port);
        bound = bind(socket_.get(), reinterpret_cast<const sockaddr*>(&any), sizeof(any));
    } else {
        sockaddr_in any{};
        any.sin_family = AF_INET;
        any.sin_addr.s_addr = htonl(INADDR_ANY);
        any.sin_port = htons(port);
        bound = bind(socket_.get(), reinterpret_cast<const sockaddr*>(&any), sizeof(any));
    }
    if (bound != 0) {
        return common::make_unexpected(NetworkError::kBindFailed);
    }
    sockaddr_storage local{};
    int local_length = sizeof(local);
    if (getsockname(socket_.get(), reinterpret_cast<sockaddr*>(&local), &local_length) != 0) {
        return common::make_unexpected(NetworkError::kBindFailed);
    }
    const auto local_address = from_sockaddr(reinterpret_cast<const sockaddr*>(&local));
    if (!local_address) {
        return common::make_unexpected(NetworkError::kBindFailed);
    }
    local_port_ = local_address->port;
    return {};
}

common::Result<void, NetworkError> WinsockUdpEndpoint::send_to(
    const UdpAddress& to, std::span<const std::uint8_t> datagram) noexcept {
    int sent = SOCKET_ERROR;
    const char* data = reinterpret_cast<const char*>(datagram.data());
    const int length = static_cast<int>(datagram.size());
    if (dual_stack_) {
        const sockaddr_in6 destination = to_sockaddr_in6(to);
        sent = sendto(socket_.get(), data, length, 0,
                      reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
    } else {
        const auto destination = to_sockaddr_in(to);
        if (!destination) {
            return common::make_unexpected(NetworkError::kAddressFamilyUnsupported);
        }
        sent = sendto(socket_.get(), data, length, 0,
                      reinterpret_cast<const sockaddr*>(&*destination), sizeof(*destination));
    }
    // 送信バッファが一杯（WSAEWOULDBLOCK）でも待たずに捨てる。UDP
    // なので、失われたパケットと同じに扱える。
    if (sent != length) {
        return common::make_unexpected(NetworkError::kSendFailed);
    }
    return {};
}

void WinsockUdpEndpoint::receive_loop() noexcept {
    // UI
    // のスレッドより先に起こされるようにし、受信からジッターバッファへの投入までの遅れを小さくする。
    // プロセスの優先度クラスは変えない（docs/adr/0008-runtime-behavior.md）。
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    const std::array<WSAEVENT, 2> events = {stop_.get(), readable_.get()};
    for (;;) {
        const DWORD signaled = WSAWaitForMultipleEvents(static_cast<DWORD>(events.size()),
                                                        events.data(), FALSE, WSA_INFINITE, FALSE);
        if (signaled != WSA_WAIT_EVENT_0 + 1) {
            return;  // 停止の要求か、待ちそのものの失敗
        }
        WSANETWORKEVENTS network_events{};
        // イベントをリセットする。FD_READ は読み残しがあれば再び通知されるが、ここでは読み切る。
        WSAEnumNetworkEvents(socket_.get(), readable_.get(), &network_events);
        drain();
    }
}

void WinsockUdpEndpoint::drain() noexcept {
    std::array<std::uint8_t, kReceiveBufferBytes> buffer{};
    for (;;) {
        sockaddr_storage from{};
        int from_length = sizeof(from);
        const int received = recvfrom(socket_.get(), reinterpret_cast<char*>(buffer.data()),
                                      static_cast<int>(buffer.size()), 0,
                                      reinterpret_cast<sockaddr*>(&from), &from_length);
        if (received == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            // 長すぎるデータグラム（WSAEMSGSIZE）と、まれに残る到達不能の通知（WSAECONNRESET）は
            // そのデータグラムだけを捨てて読み続ける。それ以外（WSAEWOULDBLOCK など）は読み終わり。
            if (error == WSAEMSGSIZE || error == WSAECONNRESET) {
                continue;
            }
            return;
        }
        const auto source = from_sockaddr(reinterpret_cast<const sockaddr*>(&from));
        if (!source) {
            continue;
        }
        listener_.on_datagram(*source, std::span<const std::uint8_t>(buffer).first(
                                           static_cast<std::size_t>(received)));
    }
}

}  // namespace vocroz::network::win
