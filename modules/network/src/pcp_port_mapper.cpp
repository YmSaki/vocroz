// SPDX-License-Identifier: AGPL-3.0-only
//
// PCP と NAT-PMP によるポート転送の設定。

#include "pcp_port_mapper.h"

#include <array>
#include <span>
#include <utility>

#include "detail/address_utils.h"
#include "detail/random_utils.h"
#include "network_constants.h"
#include "wire/nat_pmp_codec.h"

namespace vocroz::network {

namespace {

// PCP の応答を受け取ったときの判定。
enum class PcpOutcome {
    kMapped,
    kFallBackToNatPmp,
};

// 要求を送り、kGatewayRetryTimeouts の間隔で再送しながら応答を待つ。
// accept が true を返した応答を受け取ったら、その長さを返す。
template <typename Accept>
[[nodiscard]] common::Result<std::size_t, NetworkError> exchange_with_retry(
    GatewayChannel& channel, std::span<const std::uint8_t> request,
    std::span<std::uint8_t> response, Accept accept) {
    for (const std::chrono::milliseconds timeout : kGatewayRetryTimeouts) {
        const auto received = channel.exchange(request, response, timeout);
        if (!received) {
            if (received.error() == NetworkError::kTimedOut) {
                continue;
            }
            return received;
        }
        if (accept(response.first(received.value()))) {
            return received;
        }
    }
    return common::make_unexpected(NetworkError::kTimedOut);
}

[[nodiscard]] NetworkError error_for_pcp_result(std::uint8_t result) noexcept {
    return result == static_cast<std::uint8_t>(wire::PcpResult::kNotAuthorized)
               ? NetworkError::kPortMappingUnsupported
               : NetworkError::kPortMappingRefused;
}

[[nodiscard]] NetworkError error_for_nat_pmp_result(std::uint16_t result) noexcept {
    return result == static_cast<std::uint16_t>(wire::NatPmpResult::kUnsupportedVersion)
               ? NetworkError::kPortMappingUnsupported
               : NetworkError::kPortMappingRefused;
}

}  // namespace

PcpPortMapper::PcpPortMapper(ChannelOpener open_channel, transport::RandomSource& random)
    : open_channel_(std::move(open_channel)), random_(random) {}

common::Result<PortMapping, NetworkError> PcpPortMapper::map(std::uint16_t internal_port,
                                                             std::chrono::seconds lifetime) {
    auto channel = open_channel_();
    if (!channel) {
        return common::make_unexpected(channel.error());
    }
    if (!nat_pmp_only_) {
        auto mapped = map_with_pcp(*channel.value(), internal_port, lifetime);
        if (mapped || !nat_pmp_only_) {
            return mapped;
        }
        // map_with_pcp() がゲートウェイを NAT-PMP だけと判定したので、続けて NAT-PMP を試す。
    }
    return map_with_nat_pmp(*channel.value(), internal_port, lifetime);
}

void PcpPortMapper::unmap() {
    if (!active_) {
        return;
    }
    const ActiveMapping mapping = *active_;
    active_.reset();
    auto channel = open_channel_();
    if (!channel) {
        return;
    }
    std::array<std::uint8_t, wire::kPcpMaxMessageBytes> response{};
    if (mapping.method == PortMappingMethod::kPcp && nonce_) {
        std::array<std::uint8_t, wire::kPcpMapMessageBytes> request{};
        const wire::PcpMapRequest map_request{*nonce_, channel.value()->local_address(),
                                              mapping.internal_port, 0, std::chrono::seconds{0}};
        if (wire::encode_pcp_map_request(map_request, request)) {
            (void)channel.value()->exchange(request, response, kGatewayRetryTimeouts.front());
        }
        return;
    }
    std::array<std::uint8_t, wire::kNatPmpMapRequestBytes> request{};
    // RFC 6886 では、削除の要求は希望する外部ポートと寿命を 0 にする。
    if (wire::encode_nat_pmp_map_request(mapping.internal_port, 0, std::chrono::seconds{0},
                                         request)) {
        (void)channel.value()->exchange(request, response, kGatewayRetryTimeouts.front());
    }
}

common::Result<PortMapping, NetworkError> PcpPortMapper::map_with_pcp(
    GatewayChannel& channel, std::uint16_t internal_port, std::chrono::seconds lifetime) {
    if (!nonce_) {
        nonce_ = detail::random_bytes<wire::PcpNonce{}.size()>(random_);
    }
    // 同じ外部ポートを希望すると、更新やルーターの再起動の後も、相手に渡した候補が変わりにくい。
    const std::uint16_t suggested =
        active_ && active_->internal_port == internal_port ? active_->external_port : internal_port;
    const wire::PcpMapRequest map_request{*nonce_, channel.local_address(), internal_port,
                                          suggested, lifetime};
    std::array<std::uint8_t, wire::kPcpMapMessageBytes> request{};
    if (!wire::encode_pcp_map_request(map_request, request)) {
        return common::make_unexpected(NetworkError::kSendFailed);
    }

    std::array<std::uint8_t, wire::kPcpMaxMessageBytes> response{};
    std::optional<wire::PcpMapResponse> parsed;
    PcpOutcome outcome = PcpOutcome::kMapped;
    const auto received =
        exchange_with_retry(channel, request, response, [&](std::span<const std::uint8_t> bytes) {
            if (wire::is_nat_pmp_response(bytes)) {
                outcome = PcpOutcome::kFallBackToNatPmp;
                return true;
            }
            auto map_response = wire::parse_pcp_map_response(bytes);
            // nonce と内部ポートが違う応答は、別の要求への応答なので待ち続ける。
            if (!map_response || map_response.value().nonce != *nonce_ ||
                map_response.value().internal_port != internal_port) {
                return false;
            }
            parsed = map_response.value();
            return true;
        });
    if (!received) {
        return common::make_unexpected(received.error());
    }
    if (outcome == PcpOutcome::kFallBackToNatPmp ||
        parsed->result == static_cast<std::uint8_t>(wire::PcpResult::kUnsupportedVersion)) {
        nat_pmp_only_ = true;
        return common::make_unexpected(NetworkError::kPortMappingUnsupported);
    }
    if (parsed->result != static_cast<std::uint8_t>(wire::PcpResult::kSuccess)) {
        return common::make_unexpected(error_for_pcp_result(parsed->result));
    }
    if (detail::is_non_public_ipv4(parsed->external)) {
        return common::make_unexpected(NetworkError::kPortMappingUnsupported);
    }
    active_ = ActiveMapping{PortMappingMethod::kPcp, internal_port, parsed->external.port};
    return PortMapping{parsed->external, parsed->lifetime, PortMappingMethod::kPcp};
}

common::Result<PortMapping, NetworkError> PcpPortMapper::map_with_nat_pmp(
    GatewayChannel& channel, std::uint16_t internal_port, std::chrono::seconds lifetime) {
    std::array<std::uint8_t, wire::kPcpMaxMessageBytes> response{};

    // NAT-PMP の転送の応答には外部アドレスが入らないので、先に外部アドレスを取得する。
    std::array<std::uint8_t, wire::kNatPmpExternalAddressRequestBytes> address_request{};
    if (!wire::encode_nat_pmp_external_address_request(address_request)) {
        return common::make_unexpected(NetworkError::kSendFailed);
    }
    std::optional<wire::NatPmpExternalAddressResponse> address;
    const auto address_received = exchange_with_retry(
        channel, address_request, response, [&](std::span<const std::uint8_t> bytes) {
            auto parsed = wire::parse_nat_pmp_external_address_response(bytes);
            if (parsed) {
                address = parsed.value();
            }
            return parsed.has_value();
        });
    if (!address_received) {
        return common::make_unexpected(address_received.error());
    }
    if (address->result != static_cast<std::uint16_t>(wire::NatPmpResult::kSuccess)) {
        return common::make_unexpected(error_for_nat_pmp_result(address->result));
    }
    if (detail::is_non_public_ipv4(address->external)) {
        return common::make_unexpected(NetworkError::kPortMappingUnsupported);
    }

    const std::uint16_t suggested =
        active_ && active_->internal_port == internal_port ? active_->external_port : internal_port;
    std::array<std::uint8_t, wire::kNatPmpMapRequestBytes> map_request{};
    if (!wire::encode_nat_pmp_map_request(internal_port, suggested, lifetime, map_request)) {
        return common::make_unexpected(NetworkError::kSendFailed);
    }
    std::optional<wire::NatPmpMapResponse> mapped;
    const auto map_received = exchange_with_retry(
        channel, map_request, response, [&](std::span<const std::uint8_t> bytes) {
            auto parsed = wire::parse_nat_pmp_map_response(bytes);
            // 失敗の短い応答は内部ポートを含まないので、内部ポートは成功のときだけ照合する。
            if (!parsed ||
                (parsed.value().result == 0 && parsed.value().internal_port != internal_port)) {
                return false;
            }
            mapped = parsed.value();
            return true;
        });
    if (!map_received) {
        return common::make_unexpected(map_received.error());
    }
    if (mapped->result != static_cast<std::uint16_t>(wire::NatPmpResult::kSuccess)) {
        return common::make_unexpected(error_for_nat_pmp_result(mapped->result));
    }
    active_ = ActiveMapping{PortMappingMethod::kNatPmp, internal_port, mapped->external_port};
    return PortMapping{address->external.with_port(mapped->external_port), mapped->lifetime,
                       PortMappingMethod::kNatPmp};
}

}  // namespace vocroz::network
