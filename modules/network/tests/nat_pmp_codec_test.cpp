// SPDX-License-Identifier: AGPL-3.0-only
//
// NAT-PMP の要求と応答のテスト。

#include "wire/nat_pmp_codec.h"

#include <array>

#include <gtest/gtest.h>

#include "gateway_responses.h"

namespace vocroz::network::wire {
namespace {

TEST(NatPmpCodecTest, EncodesExternalAddressRequest) {
    std::array<std::uint8_t, kNatPmpExternalAddressRequestBytes> out{};
    ASSERT_TRUE(encode_nat_pmp_external_address_request(out));
    EXPECT_EQ(out, (std::array<std::uint8_t, 2>{0, 0}));
}

TEST(NatPmpCodecTest, EncodesMapRequest) {
    std::array<std::uint8_t, kNatPmpMapRequestBytes> out{};
    ASSERT_TRUE(encode_nat_pmp_map_request(40000, 40001, std::chrono::seconds{3600}, out));
    const std::array<std::uint8_t, 12> expected = {0,    1,    0,    0,    0x9c, 0x40,
                                                   0x9c, 0x41, 0x00, 0x00, 0x0e, 0x10};
    EXPECT_EQ(out, expected);
}

TEST(NatPmpCodecTest, ParsesExternalAddressResponse) {
    const auto parsed = parse_nat_pmp_external_address_response(
        test::nat_pmp_external_address(0, {203, 0, 113, 7}));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value().result, 0);
    EXPECT_EQ(parsed.value().external, UdpAddress::from_ipv4({203, 0, 113, 7}, 0));
}

TEST(NatPmpCodecTest, ParsesMapResponse) {
    const auto parsed = parse_nat_pmp_map_response(test::nat_pmp_map(0, 40000, 40001, 3600));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value().internal_port, 40000);
    EXPECT_EQ(parsed.value().external_port, 40001);
    EXPECT_EQ(parsed.value().lifetime, std::chrono::seconds{3600});
}

TEST(NatPmpCodecTest, AcceptsShortErrorResponse) {
    // 失敗の応答を結果コードと epoch までしか返さないゲートウェイがある。
    const auto parsed = parse_nat_pmp_map_response(test::nat_pmp_unsupported_version());
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value().result, 1);
}

TEST(NatPmpCodecTest, RejectsWrongOpcodeAndLength) {
    // 外部アドレスの応答を転送の応答としてパースする。
    EXPECT_FALSE(parse_nat_pmp_map_response(test::nat_pmp_external_address(0, {1, 2, 3, 4})));
    auto bytes = test::nat_pmp_map(0, 40000, 40001, 3600);
    bytes.pop_back();
    EXPECT_FALSE(parse_nat_pmp_map_response(bytes));
}

}  // namespace
}  // namespace vocroz::network::wire
