// SPDX-License-Identifier: AGPL-3.0-only
//
// 1 つの相手へ送る DatagramSocket のテスト。

#include "vocroz/network/peer_datagram_socket.h"

#include <gtest/gtest.h>

#include "fakes/fake_udp_endpoint.h"

namespace vocroz::network {
namespace {

const UdpAddress kFirst = UdpAddress::from_ipv4({203, 0, 113, 9}, 40001);
const UdpAddress kSecond = UdpAddress::from_ipv4({203, 0, 113, 9}, 40999);
const UdpAddress kThird = UdpAddress::from_ipv4({192, 168, 1, 20}, 40001);

TEST(PeerDatagramSocketTest, SendsToRemote) {
    FakeUdpEndpoint endpoint;
    PeerDatagramSocket socket(endpoint, kFirst);
    const std::vector<std::uint8_t> packet = {1, 2, 3};
    ASSERT_TRUE(socket.send(packet));
    ASSERT_EQ(endpoint.sent.size(), 1u);
    EXPECT_EQ(endpoint.sent[0].to, kFirst);
    EXPECT_EQ(endpoint.sent[0].datagram, packet);
}

TEST(PeerDatagramSocketTest, AppliesLatestRemoteUpdateOnNextSend) {
    FakeUdpEndpoint endpoint;
    PeerDatagramSocket socket(endpoint, kFirst);
    ASSERT_TRUE(socket.update_remote(kSecond));
    ASSERT_TRUE(socket.update_remote(kThird));
    ASSERT_TRUE(socket.send(std::vector<std::uint8_t>{1}));
    ASSERT_TRUE(socket.send(std::vector<std::uint8_t>{2}));
    EXPECT_EQ(endpoint.sent[0].to, kThird);
    EXPECT_EQ(endpoint.sent[1].to, kThird);
}

TEST(PeerDatagramSocketTest, ReportsSendFailure) {
    FakeUdpEndpoint endpoint;
    endpoint.fail_sends = true;
    PeerDatagramSocket socket(endpoint, kFirst);
    const auto sent = socket.send(std::vector<std::uint8_t>{1});
    ASSERT_FALSE(sent);
    EXPECT_EQ(sent.error(), transport::TransportError::kSendFailed);
}

}  // namespace
}  // namespace vocroz::network
