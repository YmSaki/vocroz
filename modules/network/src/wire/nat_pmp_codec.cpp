// SPDX-License-Identifier: AGPL-3.0-only
//
// NAT-PMP の要求と応答。

#include "wire/nat_pmp_codec.h"

#include <limits>

#include "detail/byte_cursor.h"

namespace vocroz::network::wire {

namespace {

constexpr std::uint8_t kNatPmpVersion = 0;
constexpr std::uint8_t kOpcodeExternalAddress = 0;
constexpr std::uint8_t kOpcodeMapUdp = 1;
// 応答のオペコードは要求のオペコードに 128 を足した値。
constexpr std::uint8_t kResponseOffset = 128;

// 応答の長さ（バイト）。
constexpr std::size_t kExternalAddressResponseBytes = 12;
constexpr std::size_t kMapResponseBytes = 16;
// 失敗の応答は、結果コードと epoch までの 8 バイトしか返さないゲートウェイがある。
constexpr std::size_t kErrorResponseBytes = 8;

[[nodiscard]] std::uint32_t to_wire_lifetime(std::chrono::seconds lifetime) noexcept {
    const auto count = lifetime.count();
    if (count <= 0) {
        return 0;
    }
    if (count > std::numeric_limits<std::uint32_t>::max()) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(count);
}

// 共通部分（バージョン、オペコード、結果コード、epoch）を読む。
// 形式が正しければ結果コードを返し、reader は epoch の後ろを指す。
[[nodiscard]] std::uint16_t read_common(detail::ByteReader& reader, std::uint8_t opcode) noexcept {
    const std::uint8_t version = reader.u8();
    const std::uint8_t response_opcode = reader.u8();
    const std::uint16_t result = reader.u16();
    (void)
        reader.u32();  // epoch。ゲートウェイの再起動の検出には使わない（寿命の半分で更新するため）
    if (version != kNatPmpVersion || response_opcode != kResponseOffset + opcode) {
        reader.fail();
    }
    return result;
}

[[nodiscard]] bool is_short_error(std::span<const std::uint8_t> datagram,
                                  std::uint16_t result) noexcept {
    return result != static_cast<std::uint16_t>(NatPmpResult::kSuccess) &&
           datagram.size() == kErrorResponseBytes;
}

}  // namespace

common::Result<std::size_t, NetworkError> encode_nat_pmp_external_address_request(
    std::span<std::uint8_t> out) noexcept {
    detail::ByteWriter writer(out);
    writer.u8(kNatPmpVersion);
    writer.u8(kOpcodeExternalAddress);
    if (!writer.ok()) {
        return common::make_unexpected(NetworkError::kSendFailed);
    }
    return writer.size();
}

common::Result<std::size_t, NetworkError> encode_nat_pmp_map_request(
    std::uint16_t internal_port, std::uint16_t suggested_external_port,
    std::chrono::seconds lifetime, std::span<std::uint8_t> out) noexcept {
    detail::ByteWriter writer(out);
    writer.u8(kNatPmpVersion);
    writer.u8(kOpcodeMapUdp);
    writer.zeros(2);
    writer.u16(internal_port);
    writer.u16(suggested_external_port);
    writer.u32(to_wire_lifetime(lifetime));
    if (!writer.ok()) {
        return common::make_unexpected(NetworkError::kSendFailed);
    }
    return writer.size();
}

common::Result<NatPmpExternalAddressResponse, NetworkError> parse_nat_pmp_external_address_response(
    std::span<const std::uint8_t> datagram) noexcept {
    detail::ByteReader reader(datagram);
    NatPmpExternalAddressResponse response;
    response.result = read_common(reader, kOpcodeExternalAddress);
    if (reader.ok() && is_short_error(datagram, response.result)) {
        return response;
    }
    const auto ip = reader.take(4);
    if (!reader.ok() || datagram.size() != kExternalAddressResponseBytes) {
        return common::make_unexpected(NetworkError::kMalformedResponse);
    }
    response.external = UdpAddress::from_ipv4({ip[0], ip[1], ip[2], ip[3]}, 0);
    return response;
}

common::Result<NatPmpMapResponse, NetworkError> parse_nat_pmp_map_response(
    std::span<const std::uint8_t> datagram) noexcept {
    detail::ByteReader reader(datagram);
    NatPmpMapResponse response;
    response.result = read_common(reader, kOpcodeMapUdp);
    if (reader.ok() && is_short_error(datagram, response.result)) {
        return response;
    }
    response.internal_port = reader.u16();
    response.external_port = reader.u16();
    response.lifetime = std::chrono::seconds{reader.u32()};
    if (!reader.ok() || datagram.size() != kMapResponseBytes) {
        return common::make_unexpected(NetworkError::kMalformedResponse);
    }
    return response;
}

}  // namespace vocroz::network::wire
