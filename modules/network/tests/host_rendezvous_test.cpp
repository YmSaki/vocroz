// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の登録とホールパンチのテスト。

#include "vocroz/network/host_rendezvous.h"

#include <gtest/gtest.h>

#include "fakes/fake_random_source.h"
#include "fakes/fake_udp_endpoint.h"
#include "rendezvous_messages.h"

namespace vocroz::network {
namespace {

using namespace std::chrono_literals;
using TimePoint = common::Clock::TimePoint;

const UdpAddress kServer = UdpAddress::from_ipv4({198, 51, 100, 1}, 3478);
const UdpAddress kClientReflexive = UdpAddress::from_ipv4({203, 0, 113, 30}, 61000);
const UdpAddress kClientLocal = UdpAddress::from_ipv4({10, 1, 1, 5}, 40000);
const RendezvousId kRendezvousId = {7, 7, 7};
const PeerId kClientPeer = {3};

class RecordingListener final : public HostRendezvousListener {
public:
    void on_rendezvous_available() override {
        events.push_back("available");
    }
    void on_rendezvous_unavailable(NetworkError cause) override {
        events.push_back(cause == NetworkError::kRendezvousCapacity ? "capacity" : "unreachable");
    }
    std::vector<std::string> events;
};

class HostRendezvousTest : public ::testing::Test {
protected:
    HostRendezvousTest() {
        random_.value = 0x55667788;
        HostRendezvousRequest request;
        request.server = kServer;
        request.rendezvous_id = kRendezvousId;
        request.local_candidates = {UdpAddress::from_ipv4({192, 168, 1, 2}, 40000)};
        host_.start(request, TimePoint{});
        tx_id_ = test::tx_id_of(endpoint_.sent.at(0).datagram);
    }

    void registered(TimePoint now) {
        host_.on_rendezvous_datagram(
            test::from(kServer, test::registered(tx_id_, false, false,
                                                 UdpAddress::from_ipv4({203, 0, 113, 9}, 50000))),
            now);
    }

    void run_until(TimePoint end) {
        for (TimePoint t = now_; t <= end; t += 10ms) {
            host_.poll(t);
            now_ = t;
        }
    }

    FakeUdpEndpoint endpoint_;
    transport::FakeRandomSource random_;
    RecordingListener listener_;
    HostRendezvous host_{endpoint_, random_, listener_};
    wire::TxId tx_id_{};
    TimePoint now_{};
};

TEST_F(HostRendezvousTest, RefreshesEveryTenSecondsAfterRegistered) {
    registered(TimePoint{} + 50ms);
    EXPECT_EQ(listener_.events, std::vector<std::string>{"available"});
    run_until(TimePoint{} + 10990ms);
    EXPECT_EQ(endpoint_.count_to(kServer), 2u);  // 0 秒と、登録前に決めた 1 秒
    run_until(TimePoint{} + 11s);
    EXPECT_EQ(endpoint_.count_to(kServer), 3u);
}

TEST_F(HostRendezvousTest, SendsEmptyDatagramsToClientCandidatesOnPunch) {
    registered(TimePoint{} + 50ms);
    host_.on_rendezvous_datagram(
        test::from(kServer, test::punch({}, kRendezvousId, kClientPeer, RendezvousRole::kClient,
                                        {kClientReflexive, kClientLocal})),
        TimePoint{} + 60ms);
    EXPECT_EQ(endpoint_.count_to(kClientReflexive), 1u);
    EXPECT_EQ(endpoint_.count_to(kClientLocal), 1u);
    EXPECT_TRUE(endpoint_.sent.back().datagram.empty());

    host_.mark_established(kClientReflexive);
    run_until(TimePoint{} + 500ms);
    EXPECT_EQ(endpoint_.count_to(kClientReflexive), 1u);
}

TEST_F(HostRendezvousTest, ReportsUnreachableAndRecovers) {
    run_until(TimePoint{} + 5s);
    EXPECT_EQ(listener_.events, std::vector<std::string>{"unreachable"});
    run_until(TimePoint{} + 8s);
    EXPECT_EQ(listener_.events.size(), 1u);  // 状態が変わらなければ重ねて知らせない
    registered(TimePoint{} + 8s);
    EXPECT_EQ(listener_.events, (std::vector<std::string>{"unreachable", "available"}));
}

TEST_F(HostRendezvousTest, ReportsLostRegistrationAfterThirtySeconds) {
    registered(TimePoint{} + 50ms);
    run_until(TimePoint{} + 30040ms);
    EXPECT_EQ(listener_.events, std::vector<std::string>{"available"});
    run_until(TimePoint{} + 30060ms);
    EXPECT_EQ(listener_.events, (std::vector<std::string>{"available", "unreachable"}));
}

TEST_F(HostRendezvousTest, ReportsCapacity) {
    host_.on_rendezvous_datagram(
        test::from(kServer, test::registered(tx_id_, true, false,
                                             UdpAddress::from_ipv4({203, 0, 113, 9}, 50000))),
        TimePoint{} + 50ms);
    EXPECT_EQ(listener_.events, std::vector<std::string>{"capacity"});
}

TEST_F(HostRendezvousTest, UpdatedCandidatesAreSentOnNextRegister) {
    const auto mapped = UdpAddress::from_ipv4({203, 0, 113, 9}, 40000);
    host_.set_local_candidates({mapped});
    run_until(TimePoint{} + 1s);
    const auto& last = endpoint_.sent.back().datagram;
    // 候補の数（1）と、候補の先頭（family 4、port 40000）。
    EXPECT_EQ(last[46], 1);
    EXPECT_EQ(last[47], 4);
    EXPECT_EQ(last[48], 0x9c);
    EXPECT_EQ(last[49], 0x40);
}

TEST_F(HostRendezvousTest, StopEndsEverything) {
    host_.stop();
    run_until(TimePoint{} + 20s);
    EXPECT_EQ(endpoint_.sent.size(), 1u);
    EXPECT_TRUE(listener_.events.empty());
    EXPECT_FALSE(host_.next_deadline());
}

}  // namespace
}  // namespace vocroz::network
