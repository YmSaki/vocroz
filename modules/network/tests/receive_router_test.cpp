// SPDX-License-Identifier: AGPL-3.0-only
//
// 受信したデータグラムの振り分けのテスト。

#include "vocroz/network/receive_router.h"

#include <gtest/gtest.h>

#include "fakes/fake_datagram_listener.h"
#include "hex_bytes.h"

namespace vocroz::network {
namespace {

const UdpAddress kServer = UdpAddress::from_ipv4({198, 51, 100, 1}, 3478);
const UdpAddress kPeer = UdpAddress::from_ipv4({203, 0, 113, 9}, 40001);

TEST(ReceiveRouterTest, PassesPeerPacketsThroughImmediately) {
    FakeDatagramListener peer;
    ReceiveRouter router(peer);
    const std::vector<std::uint8_t> packet = {1, 4, 0, 0, 0, 1};
    router.on_datagram(kPeer, packet);
    const auto received = peer.received();
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].from, kPeer);
    EXPECT_EQ(received[0].datagram, packet);
    EXPECT_FALSE(router.pop_rendezvous());
}

TEST(ReceiveRouterTest, QueuesRendezvousMessages) {
    FakeDatagramListener peer;
    ReceiveRouter router(peer);
    const auto registered = test::hex_bytes("56525a0102a0a1a2a3a4a5a6a7000104c350cb007107");
    router.on_datagram(kServer, registered);
    EXPECT_TRUE(peer.received().empty());
    const auto queued = router.pop_rendezvous();
    ASSERT_TRUE(queued);
    EXPECT_EQ(queued->from, kServer);
    EXPECT_EQ(std::vector<std::uint8_t>(queued->view().begin(), queued->view().end()), registered);
    EXPECT_FALSE(router.pop_rendezvous());
}

TEST(ReceiveRouterTest, DropsEmptyAndOversizedRendezvousDatagrams) {
    FakeDatagramListener peer;
    ReceiveRouter router(peer);
    router.on_datagram(kPeer, {});
    std::vector<std::uint8_t> oversized(kMaxRendezvousDatagramBytes + 1, 0);
    oversized[0] = 'V';
    oversized[1] = 'R';
    oversized[2] = 'Z';
    router.on_datagram(kServer, oversized);
    EXPECT_TRUE(peer.received().empty());
    EXPECT_FALSE(router.pop_rendezvous());
}

TEST(ReceiveRouterTest, DropsRendezvousMessagesWhenQueueIsFull) {
    FakeDatagramListener peer;
    ReceiveRouter router(peer);
    const auto registered = test::hex_bytes("56525a0102a0a1a2a3a4a5a6a7000104c350cb007107");
    for (int i = 0; i < 100; ++i) {
        router.on_datagram(kServer, registered);
    }
    int popped = 0;
    while (router.pop_rendezvous()) {
        ++popped;
    }
    EXPECT_GT(popped, 0);
    EXPECT_LT(popped, 100);
}

}  // namespace
}  // namespace vocroz::network
