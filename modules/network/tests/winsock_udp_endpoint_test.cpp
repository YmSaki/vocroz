// SPDX-License-Identifier: AGPL-3.0-only
//
// Winsock の UDP ソケットのテスト。ループバックで実際に送受信し、IPv4 と IPv6 の両方を
// 1 本のデュアルスタックのソケットで扱えること、受信がネットワーク受信スレッドから届くこと、
// 破棄で受信スレッドが止まることを確かめる。Windows でだけビルドする。

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "vocroz/network/factories.h"
#include "vocroz/network/peer_datagram_socket.h"
#include "vocroz/network/receive_router.h"
#include "vocroz/transport/constants.h"

#include "fakes/fake_datagram_listener.h"

namespace vocroz::network {
namespace {

using namespace std::chrono_literals;

constexpr auto kWait = 2s;

UdpAddress ipv4_loopback(std::uint16_t port) {
    return UdpAddress::from_ipv4({127, 0, 0, 1}, port);
}

UdpAddress ipv6_loopback(std::uint16_t port) {
    Ipv6Bytes ip{};
    ip[15] = 1;
    return UdpAddress::from_ipv6(ip, port);
}

TEST(WinsockUdpEndpointTest, SendsAndReceivesOverIpv4Loopback) {
    FakeDatagramListener a_listener;
    FakeDatagramListener b_listener;
    auto a = make_winsock_udp_endpoint(0, a_listener);
    auto b = make_winsock_udp_endpoint(0, b_listener);
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    ASSERT_NE(a.value()->local_port(), 0);

    const std::vector<std::uint8_t> payload = {1, 2, 3, 4};
    ASSERT_TRUE(a.value()->send_to(ipv4_loopback(b.value()->local_port()), payload));
    const auto received = b_listener.wait_for(1, kWait);
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].datagram, payload);
    // デュアルスタックのソケットでも、IPv4 の送信元は IPv4 として届く。
    EXPECT_EQ(received[0].from, ipv4_loopback(a.value()->local_port()));
}

TEST(WinsockUdpEndpointTest, SendsAndReceivesOverIpv6Loopback) {
    FakeDatagramListener a_listener;
    FakeDatagramListener b_listener;
    auto a = make_winsock_udp_endpoint(0, a_listener);
    auto b = make_winsock_udp_endpoint(0, b_listener);
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    const std::vector<std::uint8_t> payload = {5, 6};
    const auto sent = a.value()->send_to(ipv6_loopback(b.value()->local_port()), payload);
    if (!sent && sent.error() == NetworkError::kAddressFamilyUnsupported) {
        GTEST_SKIP() << "IPv6 を使えない環境";
    }
    ASSERT_TRUE(sent);
    const auto received = b_listener.wait_for(1, kWait);
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].datagram, payload);
    EXPECT_EQ(received[0].from, ipv6_loopback(a.value()->local_port()));
}

TEST(WinsockUdpEndpointTest, DeliversEmptyAndMaximumSizeDatagrams) {
    FakeDatagramListener a_listener;
    FakeDatagramListener b_listener;
    auto a = make_winsock_udp_endpoint(0, a_listener);
    auto b = make_winsock_udp_endpoint(0, b_listener);
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    const auto to = ipv4_loopback(b.value()->local_port());
    ASSERT_TRUE(a.value()->send_to(to, {}));
    const std::vector<std::uint8_t> largest(transport::kMaxPacketBytes, 0x5a);
    ASSERT_TRUE(a.value()->send_to(to, largest));
    const auto received = b_listener.wait_for(2, kWait);
    ASSERT_EQ(received.size(), 2u);
    EXPECT_TRUE(received[0].datagram.empty());
    EXPECT_EQ(received[1].datagram, largest);
}

TEST(WinsockUdpEndpointTest, KeepsReceivingAfterSendingToClosedPort) {
    // 閉じたポートへ送ると ICMP の到達不能が返る。SIO_UDP_CONNRESET を無効にしていないと、
    // 以降の受信が WSAECONNRESET で失敗する。
    FakeDatagramListener a_listener;
    FakeDatagramListener b_listener;
    auto a = make_winsock_udp_endpoint(0, a_listener);
    std::uint16_t closed_port = 0;
    {
        FakeDatagramListener closed_listener;
        auto closed = make_winsock_udp_endpoint(0, closed_listener);
        ASSERT_TRUE(closed);
        closed_port = closed.value()->local_port();
    }
    auto b = make_winsock_udp_endpoint(0, b_listener);
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    ASSERT_TRUE(a.value()->send_to(ipv4_loopback(closed_port), std::vector<std::uint8_t>{1}));
    std::this_thread::sleep_for(100ms);
    ASSERT_TRUE(
        b.value()->send_to(ipv4_loopback(a.value()->local_port()), std::vector<std::uint8_t>{2}));
    const auto received = a_listener.wait_for(1, kWait);
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].datagram, std::vector<std::uint8_t>{2});
}

TEST(WinsockUdpEndpointTest, RejectsPortInUse) {
    FakeDatagramListener listener;
    auto first = make_winsock_udp_endpoint(0, listener);
    ASSERT_TRUE(first);
    auto second = make_winsock_udp_endpoint(first.value()->local_port(), listener);
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error(), NetworkError::kBindFailed);
}

TEST(WinsockUdpEndpointTest, RoutesPeerPacketsAndRendezvousMessagesFromOneSocket) {
    // 親機と子機が 1 本ずつのソケットで、ランデブーメッセージと相手のパケットを同時に扱う。
    FakeDatagramListener peer_packets;
    ReceiveRouter router(peer_packets);
    FakeDatagramListener other_listener;
    auto host = make_winsock_udp_endpoint(0, router);
    auto other = make_winsock_udp_endpoint(0, other_listener);
    ASSERT_TRUE(host);
    ASSERT_TRUE(other);
    const auto to_host = ipv4_loopback(host.value()->local_port());

    PeerDatagramSocket socket(*other.value(), to_host);
    const std::vector<std::uint8_t> packet = {1, 4, 0, 0, 0, 9};
    ASSERT_TRUE(socket.send(packet));
    const std::vector<std::uint8_t> registered = {'V', 'R', 'Z', 1, 2, 0, 0, 0,   0, 0, 0,
                                                  0,   0,   0,   0, 4, 0, 1, 127, 0, 0, 1};
    ASSERT_TRUE(other.value()->send_to(to_host, registered));

    const auto received = peer_packets.wait_for(1, kWait);
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].datagram, packet);
    std::optional<RendezvousDatagram> queued;
    for (int i = 0; i < 200 && !queued; ++i) {
        queued = router.pop_rendezvous();
        if (!queued) {
            std::this_thread::sleep_for(10ms);
        }
    }
    ASSERT_TRUE(queued);
    EXPECT_EQ(queued->from, ipv4_loopback(other.value()->local_port()));
    EXPECT_EQ(queued->size, registered.size());
}

}  // namespace
}  // namespace vocroz::network
