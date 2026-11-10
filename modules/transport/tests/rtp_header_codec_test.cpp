// SPDX-License-Identifier: AGPL-3.0-only
//
// RTP 固定ヘッダーの書き込みのテスト。バイト配置を RFC 3550 の図と照らし合わせる。

#include "wire/rtp_header_codec.h"

#include <array>

#include <gtest/gtest.h>

namespace vocroz::transport::wire {
namespace {

TEST(RtpHeaderCodecTest, WritesVersionMarkerPayloadTypeAndFieldsInNetworkByteOrder) {
    RtpHeader header;
    header.marker = true;
    header.payload_type = 111;
    header.sequence = 0xBEEF;
    header.timestamp = 0x01020304;
    header.ssrc = 0xA1B2C3D4;
    std::array<std::uint8_t, kRtpHeaderBytes> out{};
    write_rtp_header(header, out);
    const std::array<std::uint8_t, kRtpHeaderBytes> expected{
        0x80, 0x80 | 111, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04, 0xA1, 0xB2, 0xC3, 0xD4};
    EXPECT_EQ(out, expected);
}

TEST(RtpHeaderCodecTest, LeavesMarkerBitClearWhenNotSet) {
    RtpHeader header;
    header.payload_type = 96;
    std::array<std::uint8_t, kRtpHeaderBytes> out{};
    write_rtp_header(header, out);
    EXPECT_EQ(out[0], 0x80);
    EXPECT_EQ(out[1], 96);
}

}  // namespace
}  // namespace vocroz::transport::wire
