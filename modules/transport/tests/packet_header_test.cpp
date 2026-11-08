// SPDX-License-Identifier: AGPL-3.0-only
//
// parse_packet_header() の単体テスト。外部から届いたヘッダーの検証を確かめる。

#include "vocroz/transport/packet_header.h"

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

namespace vocroz::transport {
namespace {

// バージョン 1、制御、接続 ID 0x01020304、シーケンス番号 0x05060708090A0B0C。
constexpr std::array<std::uint8_t, kPacketHeaderBytes> kControlHeader{
    0x01, 0x04, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};

TEST(PacketHeaderTest, ParsesBigEndianFields) {
    const auto header = parse_packet_header(kControlHeader);
    ASSERT_TRUE(header);
    EXPECT_EQ(header.value().type, PacketType::kControl);
    EXPECT_EQ(header.value().connection_id, 0x01020304U);
    EXPECT_EQ(header.value().sequence, 0x05060708090A0B0CULL);
}

TEST(PacketHeaderTest, RejectsPacketShorterThanHeader) {
    const auto header =
        parse_packet_header(std::span(kControlHeader).first(kPacketHeaderBytes - 1));
    ASSERT_FALSE(header);
    EXPECT_EQ(header.error(), TransportError::kTruncated);
}

TEST(PacketHeaderTest, RejectsEmptyPacket) {
    const auto header = parse_packet_header({});
    ASSERT_FALSE(header);
    EXPECT_EQ(header.error(), TransportError::kTruncated);
}

TEST(PacketHeaderTest, RejectsUnsupportedVersion) {
    auto bytes = kControlHeader;
    bytes[0] = 2;
    const auto header = parse_packet_header(bytes);
    ASSERT_FALSE(header);
    EXPECT_EQ(header.error(), TransportError::kUnsupportedVersion);
}

TEST(PacketHeaderTest, RejectsUnknownPacketType) {
    for (const std::uint8_t type : {std::uint8_t{0}, std::uint8_t{5}, std::uint8_t{0xFF}}) {
        auto bytes = kControlHeader;
        bytes[1] = type;
        const auto header = parse_packet_header(bytes);
        ASSERT_FALSE(header) << "type=" << static_cast<int>(type);
        EXPECT_EQ(header.error(), TransportError::kUnknownPacketType);
    }
}

}  // namespace
}  // namespace vocroz::transport
