// SPDX-License-Identifier: AGPL-3.0-only
//
// UdpFaultProxy の実装（POSIX のソケットと poll）。

#include "udp_fault_proxy.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <system_error>
#include <utility>

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace vocroz::tools {
namespace {

// 受信バッファの大きさ。UDP のデータグラムの最大長に合わせ、製品のパケット（1232 バイト以下）
// 以外が来ても切り詰めずに中継する。
constexpr std::size_t kReceiveBufferBytes = 65'536;

std::uint64_t endpoint_key(const sockaddr_in& address) {
    return (static_cast<std::uint64_t>(address.sin_addr.s_addr) << 16U) | address.sin_port;
}

int open_udp_socket(const sockaddr_in* bind_to) {
    const int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    if (bind_to != nullptr &&
        ::bind(fd, reinterpret_cast<const sockaddr*>(bind_to), sizeof(*bind_to)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

}  // namespace

std::optional<sockaddr_in> parse_ipv4_endpoint(std::string_view text) {
    const auto colon = text.rfind(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string host(text.substr(0, colon));
    const std::string_view port_text = text.substr(colon + 1);
    unsigned port = 0;
    const auto* const end = port_text.data() + port_text.size();
    const auto [ptr, ec] = std::from_chars(port_text.data(), end, port);
    if (ec != std::errc{} || ptr != end || port > 65535) {
        return std::nullopt;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        return std::nullopt;
    }
    return address;
}

common::Result<std::unique_ptr<UdpFaultProxy>, std::string> UdpFaultProxy::create(
    const UdpFaultProxyConfig& config) {
    const int fd = open_udp_socket(&config.listen);
    if (fd < 0) {
        return common::make_unexpected("cannot bind the listen address: " +
                                       std::string(std::strerror(errno)));
    }
    return std::make_unique<UdpFaultProxy>(config, fd);
}

UdpFaultProxy::UdpFaultProxy(const UdpFaultProxyConfig& config, int listen_fd) noexcept
    : config_(config),
      listen_fd_(listen_fd),
      to_upstream_(config.to_upstream),
      to_client_(config.to_client) {}

UdpFaultProxy::~UdpFaultProxy() {
    for (auto& [key, client] : clients_) {
        ::close(client.upstream_fd);
    }
    ::close(listen_fd_);
}

sockaddr_in UdpFaultProxy::listen_address() const {
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &length);
    return address;
}

void UdpFaultProxy::poll_once(std::chrono::milliseconds max_wait) {
    std::vector<pollfd> fds;
    std::vector<Client*> owners;
    fds.push_back({.fd = listen_fd_, .events = POLLIN, .revents = 0});
    owners.push_back(nullptr);
    for (auto& [key, client] : clients_) {
        fds.push_back({.fd = client.upstream_fd, .events = POLLIN, .revents = 0});
        owners.push_back(&client);
    }
    auto wait = max_wait;
    if (!pending_.empty()) {
        const auto until_due =
            std::chrono::ceil<std::chrono::milliseconds>(pending_.top().due - Clock::now());
        wait = std::max(std::chrono::milliseconds{0}, std::min(wait, until_due));
    }
    const int ready = ::poll(fds.data(), fds.size(), static_cast<int>(wait.count()));
    if (ready > 0) {
        for (std::size_t i = 0; i < fds.size(); ++i) {
            if ((fds[i].revents & POLLIN) == 0) {
                continue;
            }
            if (owners[i] == nullptr) {
                receive_from_clients();
            } else {
                receive_from_upstream(*owners[i]);
            }
        }
    }
    flush_due();
}

void UdpFaultProxy::receive_from_clients() {
    std::array<std::uint8_t, kReceiveBufferBytes> buffer{};
    while (true) {
        sockaddr_in from{};
        socklen_t length = sizeof(from);
        const auto size = ::recvfrom(listen_fd_, buffer.data(), buffer.size(), 0,
                                     reinterpret_cast<sockaddr*>(&from), &length);
        if (size < 0) {
            return;
        }
        ++stats_.received;
        Client* const client = find_or_create_client(from);
        if (client == nullptr) {
            continue;
        }
        schedule(to_upstream_, client->upstream_fd, config_.upstream, buffer.data(),
                 static_cast<std::size_t>(size));
    }
}

void UdpFaultProxy::receive_from_upstream(Client& client) {
    std::array<std::uint8_t, kReceiveBufferBytes> buffer{};
    while (true) {
        const auto size = ::recv(client.upstream_fd, buffer.data(), buffer.size(), 0);
        if (size < 0) {
            return;
        }
        ++stats_.received;
        // 応答は待ち受けのソケットから返し、子機からはプロキシのアドレスと通信しているように見せる。
        schedule(to_client_, listen_fd_, client.address, buffer.data(),
                 static_cast<std::size_t>(size));
    }
}

void UdpFaultProxy::schedule(netsim::FaultInjector& injector, int fd, const sockaddr_in& to,
                             const std::uint8_t* data, std::size_t size) {
    const netsim::FaultDecision decision = injector.decide();
    if (decision.lost) {
        ++stats_.lost;
        return;
    }
    stats_.duplicated += decision.duplicated ? 1 : 0;
    const auto now = Clock::now();
    for (std::size_t i = 0; i < decision.count; ++i) {
        const netsim::Delivery& delivery = decision.deliveries[i];
        std::vector<std::uint8_t> copy(data, data + size);
        if (delivery.corrupt) {
            ++stats_.corrupted;
            netsim::corrupt_packet(copy, delivery.corrupt_selector);
        }
        pending_.push(Pending{.due = now + delivery.delay,
                              .order = next_order_++,
                              .fd = fd,
                              .to = to,
                              .datagram = std::move(copy)});
    }
}

void UdpFaultProxy::flush_due() {
    const auto now = Clock::now();
    while (!pending_.empty() && pending_.top().due <= now) {
        const Pending& next = pending_.top();
        // 送信の失敗（相手のポートが閉じているなど）は、経路上の欠損と同じに扱って捨てる。
        if (::sendto(next.fd, next.datagram.data(), next.datagram.size(), 0,
                     reinterpret_cast<const sockaddr*>(&next.to), sizeof(next.to)) >= 0) {
            ++stats_.forwarded;
        }
        pending_.pop();
    }
}

UdpFaultProxy::Client* UdpFaultProxy::find_or_create_client(const sockaddr_in& from) {
    const auto key = endpoint_key(from);
    if (const auto found = clients_.find(key); found != clients_.end()) {
        return &found->second;
    }
    const int fd = open_udp_socket(nullptr);
    if (fd < 0) {
        return nullptr;
    }
    ++stats_.clients;
    return &clients_.emplace(key, Client{.address = from, .upstream_fd = fd}).first->second;
}

}  // namespace vocroz::tools
