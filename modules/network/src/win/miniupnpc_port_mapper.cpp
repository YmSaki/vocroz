// SPDX-License-Identifier: AGPL-3.0-only
//
// miniupnpc による UPnP-IGD のポート転送。

#include "win/miniupnpc_port_mapper.h"

#include <array>
#include <cstring>
#include <string>

#include <miniupnpc.h>
#include <upnpcommands.h>
#include <upnperrors.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "detail/address_utils.h"

namespace vocroz::network::win {

namespace {

// SSDP の応答を待つ時間（ミリ秒）。家庭用ルーターは通常 1 秒以内に応答する。
constexpr int kDiscoveryTimeoutMs = 2000;

// SSDP のマルチキャストの TTL。UPnP Device Architecture 1.1 の推奨値。
constexpr unsigned char kSsdpTtl = 2;

// UPnP のエラーコード（UPnP IGD の WANIPConnection の定義）。
constexpr int kUpnpConflictInMappingEntry = 718;
constexpr int kUpnpOnlyPermanentLeasesSupported = 725;

// 希望した外部ポートがほかの機器に使われていたとき、隣のポートを試す回数。
constexpr std::uint16_t kConflictRetries = 4;

// ルーターの転送の一覧に表示される説明。
constexpr const char* kMappingDescription = "VOCROZ";

constexpr const char* kProtocolUdp = "UDP";

// SOAP の引数は文字列で渡す。
[[nodiscard]] std::string to_decimal(std::uint64_t value) {
    return std::to_string(value);
}

[[nodiscard]] std::optional<UdpAddress> parse_ipv4(const char* text) noexcept {
    in_addr address{};
    if (text == nullptr || inet_pton(AF_INET, text, &address) != 1) {
        return std::nullopt;
    }
    Ipv4Bytes bytes{};
    std::memcpy(bytes.data(), &address, bytes.size());
    return UdpAddress::from_ipv4(bytes, 0);
}

}  // namespace

struct MiniupnpcPortMapper::Gateway {
    UPNPUrls urls{};
    IGDdatas data{};
    std::array<char, 64> lan_address{};
    std::array<char, 64> wan_address{};

    Gateway() = default;
    Gateway(const Gateway&) = delete;
    Gateway& operator=(const Gateway&) = delete;
    ~Gateway() {
        FreeUPNPUrls(&urls);
    }
};

MiniupnpcPortMapper::MiniupnpcPortMapper() = default;

MiniupnpcPortMapper::~MiniupnpcPortMapper() = default;

common::Result<PortMapping, NetworkError> MiniupnpcPortMapper::map(std::uint16_t internal_port,
                                                                   std::chrono::seconds lifetime) {
    if (!runtime_.ok()) {
        return common::make_unexpected(NetworkError::kSocketUnavailable);
    }
    if (!gateway_) {
        if (auto found = discover(); !found) {
            return common::make_unexpected(found.error());
        }
    }
    const auto external_ip = parse_ipv4(gateway_->wan_address.data());
    if (!external_ip || detail::is_non_public_ipv4(*external_ip)) {
        return common::make_unexpected(NetworkError::kPortMappingUnsupported);
    }

    const std::string internal = to_decimal(internal_port);
    std::string lease = to_decimal(static_cast<std::uint64_t>(lifetime.count()));
    const std::uint16_t first_external = mapped_external_port_.value_or(internal_port);
    for (std::uint16_t attempt = 0; attempt <= kConflictRetries; ++attempt) {
        const auto external_port = static_cast<std::uint16_t>(first_external + attempt);
        const std::string external = to_decimal(external_port);
        int result =
            UPNP_AddPortMapping(gateway_->urls.controlURL, gateway_->data.first.servicetype,
                                external.c_str(), internal.c_str(), gateway_->lan_address.data(),
                                kMappingDescription, kProtocolUdp, nullptr, lease.c_str());
        // 期限付きの転送に対応していないルーターには無期限で設定し、解除は unmap() に任せる。
        if (result == kUpnpOnlyPermanentLeasesSupported) {
            lease = "0";
            result = UPNP_AddPortMapping(gateway_->urls.controlURL,
                                         gateway_->data.first.servicetype, external.c_str(),
                                         internal.c_str(), gateway_->lan_address.data(),
                                         kMappingDescription, kProtocolUdp, nullptr, lease.c_str());
        }
        if (result == UPNPCOMMAND_SUCCESS) {
            mapped_external_port_ = external_port;
            return PortMapping{external_ip->with_port(external_port),
                               std::chrono::seconds{std::stoll(lease)},
                               PortMappingMethod::kUpnpIgd};
        }
        if (result != kUpnpConflictInMappingEntry) {
            break;
        }
    }
    // 失敗したルーターの情報は捨て、次の map() で探し直す（ルーターの再起動や交換に備える）。
    gateway_.reset();
    return common::make_unexpected(NetworkError::kPortMappingRefused);
}

void MiniupnpcPortMapper::unmap() {
    if (!gateway_ || !mapped_external_port_) {
        return;
    }
    const std::string external = to_decimal(*mapped_external_port_);
    UPNP_DeletePortMapping(gateway_->urls.controlURL, gateway_->data.first.servicetype,
                           external.c_str(), kProtocolUdp, nullptr);
    mapped_external_port_.reset();
}

common::Result<void, NetworkError> MiniupnpcPortMapper::discover() {
    int error = 0;
    UPNPDev* devices = upnpDiscover(kDiscoveryTimeoutMs, nullptr, nullptr, UPNP_LOCAL_PORT_ANY, 0,
                                    kSsdpTtl, &error);
    if (devices == nullptr) {
        return common::make_unexpected(NetworkError::kPortMappingUnsupported);
    }
    auto gateway = std::make_unique<Gateway>();
    const int status =
        UPNP_GetValidIGD(devices, &gateway->urls, &gateway->data, gateway->lan_address.data(),
                         static_cast<int>(gateway->lan_address.size()), gateway->wan_address.data(),
                         static_cast<int>(gateway->wan_address.size()));
    freeUPNPDevlist(devices);
    // UPNP_PRIVATEIP_IGD は、ルーターの外部アドレスがプライベートアドレス（二重 NAT）で、
    // 転送しても外から届かない。それ以外の接続されていないルーターも使えない。
    if (status != UPNP_CONNECTED_IGD) {
        return common::make_unexpected(NetworkError::kPortMappingUnsupported);
    }
    gateway_ = std::move(gateway);
    return {};
}

}  // namespace vocroz::network::win
