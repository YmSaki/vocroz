// SPDX-License-Identifier: AGPL-3.0-only
//
// Win32 の NetworkEnvironment のテスト。CI の環境に依存しない範囲（数値の IP アドレスの解決、
// インターフェースの列挙が失敗しないこと）だけを確かめる。Windows でだけビルドする。

#include <gtest/gtest.h>

#include "vocroz/network/factories.h"

namespace vocroz::network {
namespace {

TEST(WinNetworkEnvironmentTest, ResolvesNumericAddresses) {
    const auto environment = make_win_network_environment();
    const auto v4 = environment->resolve("127.0.0.1", 3478);
    ASSERT_TRUE(v4);
    ASSERT_EQ(v4.value().size(), 1u);
    EXPECT_EQ(v4.value()[0], UdpAddress::from_ipv4({127, 0, 0, 1}, 3478));
}

TEST(WinNetworkEnvironmentTest, FailsToResolveInvalidName) {
    const auto environment = make_win_network_environment();
    const auto resolved = environment->resolve("invalid..name", 3478);
    ASSERT_FALSE(resolved);
    EXPECT_EQ(resolved.error(), NetworkError::kResolveFailed);
}

TEST(WinNetworkEnvironmentTest, ListsInterfaceAddressesWithoutPorts) {
    const auto environment = make_win_network_environment();
    for (const UdpAddress& address : environment->interface_addresses()) {
        EXPECT_EQ(address.port, 0);
    }
}

}  // namespace
}  // namespace vocroz::network
