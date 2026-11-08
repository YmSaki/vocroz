// SPDX-License-Identifier: AGPL-3.0-only
//
// PCP の MAP 要求と応答。

#include "wire/pcp_codec.h"

#include <algorithm>
#include <limits>

#include "detail/byte_cursor.h"

namespace vocroz::network::wire {

namespace {

constexpr std::uint8_t kPcpVersion = 2;
constexpr std::uint8_t kOpcodeMap = 1;
// 応答の 2 バイト目は R ビット（最上位）とオペコード。
constexpr std::uint8_t kResponseBit = 0x80;
// IANA のプロトコル番号で UDP。
constexpr std::uint8_t kProtocolUdp = 17;

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

}  // namespace

common::Result<std::size_t, NetworkError> encode_pcp_map_request(
    const PcpMapRequest& request, std::span<std::uint8_t> out) noexcept {
    detail::ByteWriter writer(out);
    // 共通ヘッダー。
    writer.u8(kPcpVersion);
    writer.u8(kOpcodeMap);
    writer.zeros(2);
    writer.u32(to_wire_lifetime(request.lifetime));
    writer.bytes(request.client.ip);
    // MAP の本体。
    writer.bytes(request.nonce);
    writer.u8(kProtocolUdp);
    writer.zeros(3);
    writer.u16(request.internal_port);
    writer.u16(request.suggested_external_port);
    // 希望する外部アドレスは「任意」。IPv4 の NAT では ::ffff:0.0.0.0 で表す。
    const UdpAddress any =
        request.client.is_ipv4() ? UdpAddress::from_ipv4({0, 0, 0, 0}, 0) : UdpAddress{};
    writer.bytes(any.ip);
    if (!writer.ok()) {
        return common::make_unexpected(NetworkError::kSendFailed);
    }
    return writer.size();
}

common::Result<PcpMapResponse, NetworkError> parse_pcp_map_response(
    std::span<const std::uint8_t> datagram) noexcept {
    detail::ByteReader reader(datagram);
    PcpMapResponse response;
    const std::uint8_t version = reader.u8();
    const std::uint8_t opcode = reader.u8();
    (void)reader.u8();  // 予約
    response.result = reader.u8();
    response.lifetime = std::chrono::seconds{reader.u32()};
    (void)
        reader.u32();  // epoch。ゲートウェイの再起動の検出には使わない（寿命の半分で更新するため）
    (void)reader.take(12);  // 予約
    const auto nonce = reader.take(response.nonce.size());
    if (!nonce.empty()) {
        std::copy(nonce.begin(), nonce.end(), response.nonce.begin());
    }
    const std::uint8_t protocol = reader.u8();
    (void)reader.take(3);  // 予約
    response.internal_port = reader.u16();
    const std::uint16_t external_port = reader.u16();
    const auto external_ip = reader.take(16);
    if (!external_ip.empty()) {
        Ipv6Bytes ip{};
        std::copy(external_ip.begin(), external_ip.end(), ip.begin());
        response.external = UdpAddress::from_ipv6(ip, external_port);
    }
    // オプションは後ろに付くことがあるので、長さは下限だけを確かめる。
    if (!reader.ok() || version != kPcpVersion || opcode != (kResponseBit | kOpcodeMap) ||
        datagram.size() > kPcpMaxMessageBytes) {
        return common::make_unexpected(NetworkError::kMalformedResponse);
    }
    // 失敗の応答では MAP の本体の値は意味を持たないので、プロトコル番号は成功のときだけ確かめる。
    if (response.result == static_cast<std::uint8_t>(PcpResult::kSuccess) &&
        protocol != kProtocolUdp) {
        return common::make_unexpected(NetworkError::kMalformedResponse);
    }
    return response;
}

bool is_nat_pmp_response(std::span<const std::uint8_t> datagram) noexcept {
    return !datagram.empty() && datagram[0] == 0;
}

}  // namespace vocroz::network::wire
