// SPDX-License-Identifier: AGPL-3.0-only
//
// RTP 固定ヘッダーの書き込み。バイト配置は RFC 3550 の 5.1 の図のとおり。

#include "wire/rtp_header_codec.h"

#include "detail/byte_order_utils.h"

namespace vocroz::transport::wire {
namespace {

// 1 バイト目: V=2（上位 2 ビット）、P=0、X=0、CC=0。
constexpr std::uint8_t kVersion2NoPaddingNoExtension = 0x80;
// 2 バイト目の最上位ビットがマーカー、残り 7 ビットがペイロードタイプ。
constexpr std::uint8_t kMarkerBit = 0x80;
constexpr std::uint8_t kPayloadTypeMask = 0x7F;

}  // namespace

void write_rtp_header(const RtpHeader& header, std::span<std::uint8_t> out) noexcept {
    out[0] = kVersion2NoPaddingNoExtension;
    out[1] = static_cast<std::uint8_t>((header.marker ? kMarkerBit : 0U) |
                                       (header.payload_type & kPayloadTypeMask));
    detail::store_big_endian<std::uint16_t>(header.sequence, out.subspan(2));
    detail::store_big_endian<std::uint32_t>(header.timestamp, out.subspan(4));
    detail::store_big_endian<std::uint32_t>(header.ssrc, out.subspan(8));
}

}  // namespace vocroz::transport::wire
