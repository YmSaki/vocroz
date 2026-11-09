// SPDX-License-Identifier: AGPL-3.0-only
//
// NetworkExternalSocketOpener の単体テスト。宛先の名前解決の結果の最初のアドレスへ送ること、
// 名前解決とソケットを開く失敗を原因別に返すことを確かめる。

#include "network_external_socket_opener.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_udp_endpoint.h"

namespace vocroz::engine {
namespace {

namespace cp = control_protocol;

// 決めた結果を返す NetworkEnvironment。
class FixedNetworkEnvironment final : public network::NetworkEnvironment {
public:
    common::Result<std::vector<network::UdpAddress>, network::NetworkError> resolve(
        std::string_view host, std::uint16_t port) const override {
        resolved_host = std::string(host);
        if (addresses.empty()) {
            return common::make_unexpected(network::NetworkError::kResolveFailed);
        }
        std::vector<network::UdpAddress> out;
        for (const auto& a : addresses) {
            out.push_back(a.with_port(port));
        }
        return out;
    }

    std::vector<network::UdpAddress> interface_addresses() const override {
        return {};
    }

    std::vector<network::UdpAddress> addresses;
    mutable std::string resolved_host;
};

const network::UdpAddress kFirst = network::UdpAddress::from_ipv4({192, 0, 2, 10}, 0);
const network::UdpAddress kSecond = network::UdpAddress::from_ipv4({192, 0, 2, 11}, 0);

TEST(NetworkExternalSocketOpenerTest, SendsToTheFirstResolvedAddress) {
    FixedNetworkEnvironment environment;
    environment.addresses = {kFirst, kSecond};
    network::FakeUdpEndpoint* endpoint = nullptr;
    NetworkExternalSocketOpener opener(environment, [&](network::DatagramListener&) {
        auto fake = std::make_unique<network::FakeUdpEndpoint>();
        endpoint = fake.get();
        return common::Result<std::unique_ptr<network::UdpEndpoint>, network::NetworkError>(
            std::move(fake));
    });

    auto socket = opener.open("encoder.example", 5004);
    ASSERT_TRUE(socket);
    EXPECT_EQ(environment.resolved_host, "encoder.example");
    const std::array<std::uint8_t, 3> datagram{1, 2, 3};
    ASSERT_TRUE(socket.value()->send(datagram));
    ASSERT_NE(endpoint, nullptr);
    ASSERT_EQ(endpoint->sent.size(), 1U);
    EXPECT_EQ(endpoint->sent[0].to, kFirst.with_port(5004));
}

TEST(NetworkExternalSocketOpenerTest, ReportsResolveAndSocketFailures) {
    FixedNetworkEnvironment environment;
    NetworkExternalSocketOpener opener(environment, [](network::DatagramListener&) {
        return common::Result<std::unique_ptr<network::UdpEndpoint>, network::NetworkError>(
            common::make_unexpected(network::NetworkError::kSocketUnavailable));
    });
    const auto unresolved = opener.open("no-such-host.example", 5004);
    ASSERT_FALSE(unresolved);
    EXPECT_EQ(unresolved.error(), cp::ExternalOutputResult::kResolveFailed);

    environment.addresses = {kFirst};
    const auto no_socket = opener.open("192.0.2.10", 5004);
    ASSERT_FALSE(no_socket);
    EXPECT_EQ(no_socket.error(), cp::ExternalOutputResult::kSendFailed);
}

TEST(NetworkExternalSocketOpenerTest, SendFailureIsReportedToTheSender) {
    FixedNetworkEnvironment environment;
    environment.addresses = {kFirst};
    NetworkExternalSocketOpener opener(environment, [](network::DatagramListener&) {
        auto fake = std::make_unique<network::FakeUdpEndpoint>();
        fake->fail_sends = true;
        return common::Result<std::unique_ptr<network::UdpEndpoint>, network::NetworkError>(
            std::move(fake));
    });
    auto socket = opener.open("192.0.2.10", 5004);
    ASSERT_TRUE(socket);
    const std::array<std::uint8_t, 1> datagram{0};
    EXPECT_FALSE(socket.value()->send(datagram));
}

}  // namespace
}  // namespace vocroz::engine
