// SPDX-License-Identifier: AGPL-3.0-only
//
// HostRendezvousDriver の単体テスト。登録の始まりと終わりだけを確かめ、REGISTER の形式と
// 送り直しは network の HostRendezvous の単体テストに任せる。

#include "host_rendezvous_driver.h"

#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_clock.h"
#include "fakes/fake_logger.h"
#include "fakes/fake_random_source.h"
#include "fakes/fake_udp_endpoint.h"

namespace vocroz::engine {
namespace {

namespace net = network;
using namespace std::chrono_literals;

const net::UdpAddress kServer = net::UdpAddress::from_ipv4({198, 51, 100, 1}, 3478);

// 決まった値を返す NetworkEnvironment。
class FixedNetworkEnvironment final : public net::NetworkEnvironment {
public:
    [[nodiscard]] common::Result<std::vector<net::UdpAddress>, net::NetworkError> resolve(
        std::string_view /*host*/, std::uint16_t /*port*/) const override {
        ++resolve_calls;
        if (fail_resolve) {
            return common::make_unexpected(net::NetworkError::kResolveFailed);
        }
        return std::vector<net::UdpAddress>{kServer};
    }

    [[nodiscard]] std::vector<net::UdpAddress> interface_addresses() const override {
        return {net::UdpAddress::from_ipv4({192, 168, 1, 2}, 0)};
    }

    mutable int resolve_calls = 0;
    bool fail_resolve = false;
};

class HostRendezvousDriverTest : public ::testing::Test {
protected:
    HostRendezvousDriver make_driver(std::string host) {
        return HostRendezvousDriver(
            HostRendezvousDriver::Ports{endpoint_, router_, environment_, random_, clock_, logger_},
            std::move(host), 3478);
    }

    static session::RendezvousId id(std::uint8_t value) {
        session::RendezvousId out{};
        out.fill(value);
        return out;
    }

    common::FakeClock clock_;
    common::FakeLogger logger_;
    net::FakeUdpEndpoint endpoint_;
    transport::FakeRandomSource random_;
    FixedNetworkEnvironment environment_;
    class NullListener final : public net::DatagramListener {
    public:
        void on_datagram(const net::UdpAddress& /*from*/,
                         std::span<const std::uint8_t> /*datagram*/) noexcept override {}
    } peers_;
    net::ReceiveRouter router_{peers_};
};

TEST_F(HostRendezvousDriverTest, StartsRegisteringWithSessionRendezvousId) {
    auto driver = make_driver("rendezvous.example");
    driver.start(id(1));
    EXPECT_TRUE(driver.active());
    EXPECT_EQ(endpoint_.count_to(kServer), 1U);
}

TEST_F(HostRendezvousDriverTest, SameIdDoesNotRestartRegistration) {
    auto driver = make_driver("rendezvous.example");
    driver.start(id(1));
    driver.start(id(1));
    EXPECT_EQ(environment_.resolve_calls, 1);
    EXPECT_EQ(endpoint_.count_to(kServer), 1U);
    driver.start(id(2));
    EXPECT_EQ(environment_.resolve_calls, 2);
}

TEST_F(HostRendezvousDriverTest, StopEndsRegistration) {
    auto driver = make_driver("rendezvous.example");
    driver.start(id(1));
    driver.stop();
    EXPECT_FALSE(driver.active());
    clock_.advance(30s);
    driver.poll();
    EXPECT_EQ(endpoint_.count_to(kServer), 1U);
}

TEST_F(HostRendezvousDriverTest, KeepsResendingWhileActive) {
    auto driver = make_driver("rendezvous.example");
    driver.start(id(1));
    clock_.advance(30s);
    driver.poll();
    EXPECT_GT(endpoint_.count_to(kServer), 1U);
}

TEST_F(HostRendezvousDriverTest, DoesNothingWithoutServer) {
    auto driver = make_driver("");
    driver.start(id(1));
    EXPECT_FALSE(driver.active());
    EXPECT_EQ(environment_.resolve_calls, 0);
    EXPECT_TRUE(endpoint_.sent.empty());
}

TEST_F(HostRendezvousDriverTest, UnresolvableServerLeavesRegistrationInactive) {
    environment_.fail_resolve = true;
    auto driver = make_driver("rendezvous.example");
    driver.start(id(1));
    EXPECT_FALSE(driver.active());
    EXPECT_TRUE(endpoint_.sent.empty());
}

}  // namespace
}  // namespace vocroz::engine
