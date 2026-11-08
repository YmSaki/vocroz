// SPDX-License-Identifier: AGPL-3.0-only
//
// UDP ホールパンチの送信のテスト。

#include "hole_puncher.h"

#include <gtest/gtest.h>

#include "fakes/fake_udp_endpoint.h"

namespace vocroz::network {
namespace {

using namespace std::chrono_literals;
using TimePoint = common::Clock::TimePoint;

const UdpAddress kReflexive = UdpAddress::from_ipv4({203, 0, 113, 9}, 50000);
const UdpAddress kLocal = UdpAddress::from_ipv4({192, 168, 1, 20}, 40001);
const PeerId kPeer = {1};
const PeerId kOtherPeer = {2};

TEST(HolePuncherTest, SendsToAllCandidatesImmediatelyAndEvery50Ms) {
    FakeUdpEndpoint endpoint;
    HolePuncher puncher(endpoint);
    const std::vector<std::uint8_t> datagram = {9, 9};
    puncher.set_datagram(datagram);
    const TimePoint t0{};
    puncher.punch(kPeer, std::vector{kReflexive, kLocal}, t0);
    EXPECT_EQ(endpoint.count_to(kReflexive), 1u);
    EXPECT_EQ(endpoint.count_to(kLocal), 1u);
    EXPECT_EQ(endpoint.sent[0].datagram, datagram);
    EXPECT_EQ(puncher.next_deadline(), t0 + 50ms);

    puncher.poll(t0 + 49ms);
    EXPECT_EQ(endpoint.count_to(kReflexive), 1u);
    puncher.poll(t0 + 50ms);
    EXPECT_EQ(endpoint.count_to(kReflexive), 2u);
    EXPECT_EQ(endpoint.count_to(kLocal), 2u);
}

TEST(HolePuncherTest, StopsAfterWindowAndExtendsOnNewPunch) {
    FakeUdpEndpoint endpoint;
    HolePuncher puncher(endpoint);
    const TimePoint t0{};
    puncher.punch(kPeer, std::vector{kReflexive}, t0);
    puncher.punch(kPeer, std::vector{kReflexive}, t0 + 3s);  // 次の PUNCH で 5 秒延びる
    puncher.poll(t0 + 6s);
    EXPECT_TRUE(puncher.active());
    puncher.poll(t0 + 8s);
    EXPECT_FALSE(puncher.active());
    EXPECT_FALSE(puncher.next_deadline());
}

TEST(HolePuncherTest, StopsOnlyTheTargetThatContainsTheAddress) {
    FakeUdpEndpoint endpoint;
    HolePuncher puncher(endpoint);
    const TimePoint t0{};
    puncher.punch(kPeer, std::vector{kReflexive, kLocal}, t0);
    const auto other = UdpAddress::from_ipv4({198, 51, 100, 3}, 40001);
    puncher.punch(kOtherPeer, std::vector{other}, t0);
    puncher.stop_for_address(kLocal);
    endpoint.sent.clear();
    puncher.poll(t0 + 50ms);
    EXPECT_EQ(endpoint.count_to(kReflexive), 0u);
    EXPECT_EQ(endpoint.count_to(other), 1u);
}

TEST(HolePuncherTest, ContinuesWhenSomeSendsFail) {
    FakeUdpEndpoint endpoint;
    endpoint.fail_sends = true;
    HolePuncher puncher(endpoint);
    puncher.punch(kPeer, std::vector{kReflexive}, TimePoint{});
    EXPECT_TRUE(puncher.active());
}

}  // namespace
}  // namespace vocroz::network
