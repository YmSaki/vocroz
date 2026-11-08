// SPDX-License-Identifier: AGPL-3.0-only
//
// PCP の MAP 要求と応答のテスト。

#include "wire/pcp_codec.h"

#include <array>

#include <gtest/gtest.h>

#include "gateway_responses.h"

namespace vocroz::network::wire {
namespace {

constexpr PcpNonce kNonce = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

TEST(PcpCodecTest, EncodesMapRequestInRfc6887Layout) {
    const PcpMapRequest request{kNonce, UdpAddress::from_ipv4({192, 168, 1, 10}, 0), 40000, 40000,
                                std::chrono::seconds{3600}};
    std::array<std::uint8_t, kPcpMapMessageBytes> out{};
    const auto written = encode_pcp_map_request(request, out);
    ASSERT_TRUE(written);
    ASSERT_EQ(written.value(), kPcpMapMessageBytes);
    EXPECT_EQ(out[0], 2);  // バージョン
    EXPECT_EQ(out[1], 1);  // R = 0、オペコード MAP
    EXPECT_EQ(out[4], 0x00);
    EXPECT_EQ(out[7], 0x10);  // 寿命 3600 = 0x00000e10
    EXPECT_EQ(out[6], 0x0e);
    // クライアントのアドレスは IPv4 射影アドレス。
    EXPECT_EQ(out[18], 0xff);
    EXPECT_EQ(out[19], 0xff);
    EXPECT_EQ(out[20], 192);
    EXPECT_EQ(out[23], 10);
    // nonce、プロトコル、ポート。
    EXPECT_TRUE(std::equal(kNonce.begin(), kNonce.end(), out.begin() + 24));
    EXPECT_EQ(out[36], 17);
    EXPECT_EQ(out[40], 0x9c);  // 40000 = 0x9c40
    EXPECT_EQ(out[41], 0x40);
    EXPECT_EQ(out[42], 0x9c);
    // 希望する外部アドレスは ::ffff:0.0.0.0（任意）。
    EXPECT_EQ(out[54], 0xff);
    EXPECT_EQ(out[55], 0xff);
    EXPECT_EQ(out[56], 0);
}

TEST(PcpCodecTest, RejectsShortOutput) {
    std::array<std::uint8_t, kPcpMapMessageBytes - 1> out{};
    EXPECT_FALSE(encode_pcp_map_request(PcpMapRequest{}, out));
}

TEST(PcpCodecTest, ParsesSuccessfulMapResponse) {
    const auto external = UdpAddress::from_ipv4({203, 0, 113, 7}, 40001);
    const auto bytes = test::pcp_map_response(0, kNonce, 40000, external, 7200);
    const auto parsed = parse_pcp_map_response(bytes);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value().result, 0);
    EXPECT_EQ(parsed.value().nonce, kNonce);
    EXPECT_EQ(parsed.value().internal_port, 40000);
    EXPECT_EQ(parsed.value().external, external);
    EXPECT_EQ(parsed.value().lifetime, std::chrono::seconds{7200});
}

TEST(PcpCodecTest, ParsesResponseWithTrailingOptions) {
    auto bytes =
        test::pcp_map_response(0, kNonce, 40000, UdpAddress::from_ipv4({203, 0, 113, 7}, 1), 60);
    bytes.insert(bytes.end(), {0x01, 0x00, 0x00, 0x00});  // 本体のないオプション
    EXPECT_TRUE(parse_pcp_map_response(bytes));
}

TEST(PcpCodecTest, RejectsRequestsAndShortResponses) {
    auto bytes =
        test::pcp_map_response(0, kNonce, 40000, UdpAddress::from_ipv4({203, 0, 113, 7}, 1), 60);
    auto request = bytes;
    request[1] = 0x01;  // R ビットのない要求
    EXPECT_FALSE(parse_pcp_map_response(request));
    bytes.pop_back();
    EXPECT_FALSE(parse_pcp_map_response(bytes));
}

TEST(PcpCodecTest, DetectsNatPmpResponse) {
    EXPECT_TRUE(is_nat_pmp_response(test::nat_pmp_unsupported_version()));
    EXPECT_FALSE(is_nat_pmp_response(
        test::pcp_map_response(0, kNonce, 1, UdpAddress::from_ipv4({203, 0, 113, 7}, 1), 60)));
}

}  // namespace
}  // namespace vocroz::network::wire
