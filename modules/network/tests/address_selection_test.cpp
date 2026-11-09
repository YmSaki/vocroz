// SPDX-License-Identifier: AGPL-3.0-only
//
// ローカル候補アドレスとランデブーサーバーのアドレスの選び方のテスト。

#include "vocroz/network/address_selection.h"

#include <array>

#include <gtest/gtest.h>

#include "vocroz/network/rendezvous_types.h"

namespace vocroz::network {
namespace {

UdpAddress v6(std::initializer_list<std::uint8_t> head, std::uint8_t last) {
    Ipv6Bytes ip{};
    std::copy(head.begin(), head.end(), ip.begin());
    ip[15] = last;
    return UdpAddress::from_ipv6(ip, 0);
}

TEST(AddressSelectionTest, OrdersMappedThenIpv6GlobalThenIpv4ThenUniqueLocal) {
    const std::array interfaces = {
        UdpAddress::from_ipv4({192, 168, 1, 10}, 0), v6({0xfd, 0x00}, 1),  // ユニークローカル
        v6({0x24, 0x09, 0x00, 0x10}, 2),                                   // グローバル
    };
    const auto mapped = UdpAddress::from_ipv4({203, 0, 113, 7}, 50000);
    const auto selected = select_local_candidates(interfaces, 40000, mapped);
    ASSERT_EQ(selected.size(), 4u);
    EXPECT_EQ(selected[0], mapped);
    EXPECT_EQ(selected[1], v6({0x24, 0x09, 0x00, 0x10}, 2).with_port(40000));
    EXPECT_EQ(selected[2], UdpAddress::from_ipv4({192, 168, 1, 10}, 40000));
    EXPECT_EQ(selected[3], v6({0xfd, 0x00}, 1).with_port(40000));
}

TEST(AddressSelectionTest, ExcludesUnreachableAddresses) {
    const std::array interfaces = {
        UdpAddress::from_ipv4({127, 0, 0, 1}, 0),    // ループバック
        UdpAddress::from_ipv4({169, 254, 3, 4}, 0),  // リンクローカル
        UdpAddress::from_ipv4({0, 0, 0, 0}, 0),      // 未指定
        v6({}, 1),                                   // ::1
        v6({0xfe, 0x80}, 5),                         // リンクローカル
        v6({0x20, 0x01, 0x00, 0x00, 0x12}, 6),       // Teredo
        v6({0xff, 0x02}, 1),                         // マルチキャスト
    };
    EXPECT_TRUE(select_local_candidates(interfaces, 40000, std::nullopt).empty());
}

TEST(AddressSelectionTest, RemovesDuplicatesAndLimitsCount) {
    std::vector<UdpAddress> interfaces;
    for (std::uint8_t i = 1; i <= 12; ++i) {
        interfaces.push_back(UdpAddress::from_ipv4({10, 0, 0, i}, 0));
        interfaces.push_back(UdpAddress::from_ipv4({10, 0, 0, i}, 0));
    }
    const auto selected = select_local_candidates(interfaces, 40000, std::nullopt);
    ASSERT_EQ(selected.size(), kMaxLocalCandidates);
    EXPECT_EQ(selected[1], UdpAddress::from_ipv4({10, 0, 0, 2}, 40000));
}

TEST(AddressSelectionTest, PrefersIpv4ServerAddress) {
    const std::array resolved = {v6({0x24, 0x09}, 1).with_port(3478),
                                 UdpAddress::from_ipv4({198, 51, 100, 1}, 3478)};
    EXPECT_EQ(select_server_address(resolved), resolved[1]);
    EXPECT_EQ(select_server_address(std::span(resolved).first(1)), resolved[0]);
    EXPECT_FALSE(select_server_address({}));
}

TEST(AddressSelectionTest, RoutableCandidateRules) {
    EXPECT_TRUE(is_routable_candidate(UdpAddress::from_ipv4({10, 0, 0, 1}, 1)));
    EXPECT_FALSE(is_routable_candidate(UdpAddress::from_ipv4({10, 0, 0, 1}, 0)));
    EXPECT_FALSE(is_routable_candidate(UdpAddress::from_ipv4({239, 0, 0, 1}, 1)));
    EXPECT_FALSE(is_routable_candidate(v6({}, 0).with_port(1)));
}

}  // namespace
}  // namespace vocroz::network
