// SPDX-License-Identifier: AGPL-3.0-only
//
// Win32 の API による NetworkEnvironment と、デフォルトゲートウェイの検出。

#include "win/win_network_environment.h"

// iphlpapi.h は winsock2.h の後に読む必要がある。
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
// clang-format on

#include <limits>
#include <memory>
#include <string>

#include "win/winsock_address.h"

namespace vocroz::network::win {

namespace {

// GetAdaptersAddresses の最初のバッファの大きさ。Microsoft の推奨値（15KB）で、
// 足りなければ API が必要な大きさを返すので取り直す。
constexpr ULONG kInitialAdapterBufferBytes = 15 * 1024;

// 取り直しの回数。取得の間にアダプターが増えたときに備える。
constexpr int kAdapterQueryAttempts = 3;

// FreeAddrInfo は Windows のヘッダーでマクロになっているので、その名前を避ける。
struct AddrInfoDeleter {
    void operator()(addrinfo* info) const noexcept {
        freeaddrinfo(info);
    }
};

// GetAdaptersAddresses の結果を取得する。失敗したら空のバッファを返す。
[[nodiscard]] std::unique_ptr<std::uint8_t[]> query_adapters(ULONG family, ULONG flags) {
    ULONG size = kInitialAdapterBufferBytes;
    for (int attempt = 0; attempt < kAdapterQueryAttempts; ++attempt) {
        auto buffer = std::make_unique<std::uint8_t[]>(size);
        const ULONG result = GetAdaptersAddresses(
            family, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.get()), &size);
        if (result == ERROR_SUCCESS) {
            return buffer;
        }
        if (result != ERROR_BUFFER_OVERFLOW) {
            break;
        }
    }
    return nullptr;
}

[[nodiscard]] bool is_usable_adapter(const IP_ADAPTER_ADDRESSES& adapter) noexcept {
    return adapter.OperStatus == IfOperStatusUp && adapter.IfType != IF_TYPE_SOFTWARE_LOOPBACK;
}

}  // namespace

common::Result<std::vector<UdpAddress>, NetworkError> WinNetworkEnvironment::resolve(
    std::string_view host, std::uint16_t port) const {
    if (!runtime_.ok()) {
        return common::make_unexpected(NetworkError::kSocketUnavailable);
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* raw = nullptr;
    const std::string name(host);
    const std::string service = std::to_string(port);
    if (getaddrinfo(name.c_str(), service.c_str(), &hints, &raw) != 0) {
        return common::make_unexpected(NetworkError::kResolveFailed);
    }
    const std::unique_ptr<addrinfo, AddrInfoDeleter> info(raw);
    std::vector<UdpAddress> out;
    for (const addrinfo* entry = info.get(); entry != nullptr; entry = entry->ai_next) {
        if (const auto address = from_sockaddr(entry->ai_addr)) {
            out.push_back(*address);
        }
    }
    if (out.empty()) {
        return common::make_unexpected(NetworkError::kResolveFailed);
    }
    return out;
}

std::vector<UdpAddress> WinNetworkEnvironment::interface_addresses() const {
    std::vector<UdpAddress> out;
    const auto buffer =
        query_adapters(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                      GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME);
    if (!buffer) {
        return out;
    }
    for (auto* adapter = reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(buffer.get());
         adapter != nullptr; adapter = adapter->Next) {
        if (!is_usable_adapter(*adapter)) {
            continue;
        }
        for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr;
             unicast = unicast->Next) {
            // 重複アドレス検出が終わっていないアドレスや、廃止予定のアドレスは相手から届かないことがある。
            if (unicast->DadState != IpDadStatePreferred) {
                continue;
            }
            if (const auto address = from_sockaddr(unicast->Address.lpSockaddr)) {
                out.push_back(address->with_port(0));
            }
        }
    }
    return out;
}

std::optional<UdpAddress> find_default_ipv4_gateway() {
    const auto buffer = query_adapters(
        AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                     GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME);
    if (!buffer) {
        return std::nullopt;
    }
    std::optional<UdpAddress> best;
    ULONG best_metric = std::numeric_limits<ULONG>::max();
    for (auto* adapter = reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(buffer.get());
         adapter != nullptr; adapter = adapter->Next) {
        if (!is_usable_adapter(*adapter) || adapter->FirstGatewayAddress == nullptr) {
            continue;
        }
        const auto gateway = from_sockaddr(adapter->FirstGatewayAddress->Address.lpSockaddr);
        if (gateway && gateway->is_ipv4() && adapter->Ipv4Metric < best_metric) {
            best = gateway->with_port(0);
            best_metric = adapter->Ipv4Metric;
        }
    }
    return best;
}

}  // namespace vocroz::network::win
