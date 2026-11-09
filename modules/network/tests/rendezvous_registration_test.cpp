// SPDX-License-Identifier: AGPL-3.0-only
//
// ランデブーサーバーへの登録のテスト。

#include "rendezvous_registration.h"

#include <gtest/gtest.h>

#include "fakes/fake_udp_endpoint.h"
#include "rendezvous_messages.h"

namespace vocroz::network {
namespace {

using namespace std::chrono_literals;
using TimePoint = common::Clock::TimePoint;

const UdpAddress kServer = UdpAddress::from_ipv4({198, 51, 100, 1}, 3478);
const RendezvousId kRendezvousId = {7, 7, 7};
const PeerId kHostPeer = {9};
constexpr wire::TxId kTxId = {1, 2, 3, 4, 5, 6, 7, 8};

class RecordingListener final : public RendezvousRegistration::Listener {
public:
    void on_registered(const wire::RegisteredMessage& message) override {
        registered.push_back(message);
    }
    void on_punch(const wire::PunchMessage& message) override {
        punches.push_back(message);
    }
    std::vector<wire::RegisteredMessage> registered;
    std::vector<wire::PunchMessage> punches;
};

class RendezvousRegistrationTest : public ::testing::Test {
protected:
    void start(std::chrono::milliseconds refresh) {
        RegistrationConfig config;
        config.server = kServer;
        config.rendezvous_id = kRendezvousId;
        config.peer_id = {5};
        config.role = RendezvousRole::kClient;
        config.candidates = {UdpAddress::from_ipv4({192, 168, 1, 20}, 40000)};
        config.refresh_interval = refresh;
        registration_.start(config, kTxId, TimePoint{});
    }

    FakeUdpEndpoint endpoint_;
    RecordingListener listener_;
    RendezvousRegistration registration_{endpoint_, listener_};
};

TEST_F(RendezvousRegistrationTest, SendsRegisterImmediatelyAndRetriesEverySecond) {
    start(10s);
    ASSERT_EQ(endpoint_.count_to(kServer), 1u);
    const auto parsed = test::tx_id_of(endpoint_.sent[0].datagram);
    EXPECT_EQ(parsed, kTxId);
    registration_.poll(TimePoint{} + 999ms);
    EXPECT_EQ(endpoint_.count_to(kServer), 1u);
    registration_.poll(TimePoint{} + 1s);
    EXPECT_EQ(endpoint_.count_to(kServer), 2u);
}

TEST_F(RendezvousRegistrationTest, UsesRefreshIntervalAfterRegistered) {
    start(10s);
    registration_.on_datagram(
        test::from(kServer, test::registered(kTxId, false, false,
                                             UdpAddress::from_ipv4({203, 0, 113, 5}, 1))),
        TimePoint{} + 100ms);
    ASSERT_EQ(listener_.registered.size(), 1u);
    EXPECT_EQ(registration_.last_registered_at(), TimePoint{} + 100ms);
    registration_.poll(TimePoint{} + 1s);  // 登録前に決めた送り直し
    registration_.poll(TimePoint{} + 2s);
    EXPECT_EQ(endpoint_.count_to(kServer), 2u);
    registration_.poll(TimePoint{} + 11s);
    EXPECT_EQ(endpoint_.count_to(kServer), 3u);
}

TEST_F(RendezvousRegistrationTest, IgnoresMessagesFromOtherSourcesOrTransactions) {
    start(1s);
    const auto reflexive = UdpAddress::from_ipv4({203, 0, 113, 5}, 1);
    const auto attacker = UdpAddress::from_ipv4({198, 51, 100, 66}, 3478);
    registration_.on_datagram(test::from(attacker, test::registered(kTxId, false, true, reflexive)),
                              TimePoint{});
    wire::TxId other = kTxId;
    other[0] = 0xff;
    registration_.on_datagram(test::from(kServer, test::registered(other, false, true, reflexive)),
                              TimePoint{});
    EXPECT_TRUE(listener_.registered.empty());
}

TEST_F(RendezvousRegistrationTest, AcceptsPunchForOwnOrZeroTransactionFromOppositeRole) {
    start(1s);
    const std::vector<UdpAddress> candidates = {UdpAddress::from_ipv4({203, 0, 113, 9}, 50000)};
    registration_.on_datagram(test::from(kServer, test::punch(kTxId, kRendezvousId, kHostPeer,
                                                              RendezvousRole::kHost, candidates)),
                              TimePoint{});
    registration_.on_datagram(test::from(kServer, test::punch({}, kRendezvousId, kHostPeer,
                                                              RendezvousRole::kHost, candidates)),
                              TimePoint{});
    ASSERT_EQ(listener_.punches.size(), 2u);
    EXPECT_EQ(listener_.punches[0].candidates, candidates);

    // 別のランデブー ID、同じ役割、別の txid は受け付けない。
    registration_.on_datagram(
        test::from(kServer, test::punch(kTxId, {8}, kHostPeer, RendezvousRole::kHost, candidates)),
        TimePoint{});
    registration_.on_datagram(test::from(kServer, test::punch(kTxId, kRendezvousId, kHostPeer,
                                                              RendezvousRole::kClient, candidates)),
                              TimePoint{});
    registration_.on_datagram(test::from(kServer, test::punch({9, 9}, kRendezvousId, kHostPeer,
                                                              RendezvousRole::kHost, candidates)),
                              TimePoint{});
    EXPECT_EQ(listener_.punches.size(), 2u);
}

TEST_F(RendezvousRegistrationTest, StopsSendingAndAccepting) {
    start(1s);
    registration_.stop();
    registration_.poll(TimePoint{} + 5s);
    EXPECT_EQ(endpoint_.count_to(kServer), 1u);
    registration_.on_datagram(
        test::from(kServer, test::registered(kTxId, false, false,
                                             UdpAddress::from_ipv4({203, 0, 113, 5}, 1))),
        TimePoint{});
    EXPECT_TRUE(listener_.registered.empty());
    EXPECT_FALSE(registration_.next_deadline());
}

}  // namespace
}  // namespace vocroz::network
