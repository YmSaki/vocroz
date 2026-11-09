// SPDX-License-Identifier: AGPL-3.0-only
//
// 相手のパケットの受け箱（PeerInbox）のテスト。

#include "peer_inbox.h"

#include <vector>

#include <gtest/gtest.h>

namespace vocroz::client {
namespace {

const network::UdpAddress kHost = network::UdpAddress::from_ipv4({192, 0, 2, 1}, 5000);

TEST(PeerInboxTest, HandsOverDatagramsInOrder) {
    PeerInbox inbox;
    inbox.on_datagram(kHost, std::vector<std::uint8_t>{1, 2});
    inbox.on_datagram(kHost, std::vector<std::uint8_t>{3});

    const auto first = inbox.pop();
    ASSERT_TRUE(first);
    EXPECT_EQ(first->from, kHost);
    EXPECT_EQ(std::vector(first->view().begin(), first->view().end()),
              (std::vector<std::uint8_t>{1, 2}));
    EXPECT_EQ(inbox.pop()->size, 1U);
    EXPECT_FALSE(inbox.pop());
}

TEST(PeerInboxTest, DropsOversizedDatagrams) {
    PeerInbox inbox;
    inbox.on_datagram(kHost, std::vector<std::uint8_t>(transport::kMaxPacketBytes + 1));
    EXPECT_FALSE(inbox.pop());
    EXPECT_EQ(inbox.dropped(), 1U);
}

TEST(PeerInboxTest, DropsWhenFull) {
    PeerInbox inbox;
    for (std::size_t i = 0; i < kPeerInboxCapacity + 1; ++i) {
        inbox.on_datagram(kHost, std::vector<std::uint8_t>{1});
    }
    EXPECT_EQ(inbox.dropped(), 1U);
}

}  // namespace
}  // namespace vocroz::client
