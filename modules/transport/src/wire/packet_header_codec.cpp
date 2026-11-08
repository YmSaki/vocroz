// SPDX-License-Identifier: AGPL-3.0-only
//
// 固定ヘッダーのパースとシリアライズ。外部から届いたバイト列を検証済みの PacketHeader に変換する
// 信頼境界（AGENTS.md 6.3）。

#include "wire/packet_header_codec.h"

#include <cstddef>

#include "detail/byte_order_utils.h"
#include "transport_constants.h"

namespace vocroz::transport {
namespace {

// ヘッダー内の各フィールドの位置（バイト）。
constexpr std::size_t kVersionOffset = 0;
constexpr std::size_t kTypeOffset = 1;
constexpr std::size_t kConnectionIdOffset = 2;
constexpr std::size_t kSequenceOffset = 6;

// 未定義の値を enum にキャストしないよう、既知の値だけを列挙して判定する。
[[nodiscard]] bool is_known_packet_type(std::uint8_t value) noexcept {
    switch (static_cast<PacketType>(value)) {
        case PacketType::kHandshakeInitiation:
        case PacketType::kHandshakeResponse:
        case PacketType::kMedia:
        case PacketType::kControl:
            return true;
    }
    return false;
}

}  // namespace

common::Result<PacketHeader, TransportError> parse_packet_header(
    std::span<const std::uint8_t> packet) noexcept {
    if (packet.size() < kPacketHeaderBytes) {
        return common::make_unexpected(TransportError::kTruncated);
    }
    if (packet[kVersionOffset] != kProtocolVersion) {
        return common::make_unexpected(TransportError::kUnsupportedVersion);
    }
    const std::uint8_t type = packet[kTypeOffset];
    if (!is_known_packet_type(type)) {
        return common::make_unexpected(TransportError::kUnknownPacketType);
    }
    return PacketHeader{
        .type = static_cast<PacketType>(type),
        .connection_id = detail::load_big_endian<ConnectionId>(packet.subspan(kConnectionIdOffset)),
        .sequence = detail::load_big_endian<std::uint64_t>(packet.subspan(kSequenceOffset)),
    };
}

namespace wire {

void write_packet_header(const PacketHeader& header, std::span<std::uint8_t> out) noexcept {
    out[kVersionOffset] = kProtocolVersion;
    out[kTypeOffset] = static_cast<std::uint8_t>(header.type);
    detail::store_big_endian(header.connection_id, out.subspan(kConnectionIdOffset));
    detail::store_big_endian(header.sequence, out.subspan(kSequenceOffset));
}

}  // namespace wire
}  // namespace vocroz::transport
