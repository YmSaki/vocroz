// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機の経路の確立のテスト。打ち切りの原因の判定（サーバーに届かない、親機がいない、
// ホールパンチが成立しない、サーバーの上限）と、鍵交換のメッセージを全候補へ送ることを確かめる。

#include "vocroz/network/client_rendezvous.h"

#include <gtest/gtest.h>

#include "fakes/fake_random_source.h"
#include "fakes/fake_udp_endpoint.h"
#include "rendezvous_messages.h"

namespace vocroz::network {
namespace {

using namespace std::chrono_literals;
using TimePoint = common::Clock::TimePoint;

const UdpAddress kServer = UdpAddress::from_ipv4({198, 51, 100, 1}, 3478);
const UdpAddress kHostReflexive = UdpAddress::from_ipv4({203, 0, 113, 9}, 50000);
const UdpAddress kHostMapped = UdpAddress::from_ipv4({203, 0, 113, 9}, 40000);
const RendezvousId kRendezvousId = {7, 7, 7};
const PeerId kHostPeer = {9};

class RecordingListener final : public ClientRendezvousListener {
public:
    void on_path_failed(NetworkError cause) override {
        failures.push_back(cause);
    }
    std::vector<NetworkError> failures;
};

class ClientRendezvousTest : public ::testing::Test {
protected:
    ClientRendezvousTest() {
        random_.value = 0x11223344;
        ClientRendezvousRequest request;
        request.server = kServer;
        request.rendezvous_id = kRendezvousId;
        request.local_candidates = {UdpAddress::from_ipv4({192, 168, 1, 20}, 40000)};
        request.opening_datagram = kInitiation;
        client_.start(request, TimePoint{});
        tx_id_ = test::tx_id_of(endpoint_.sent.at(0).datagram);
    }

    void deliver(const std::vector<std::uint8_t>& bytes, TimePoint now) {
        client_.on_rendezvous_datagram(test::from(kServer, bytes), now);
    }

    void registered(TimePoint now) {
        deliver(test::registered(tx_id_, false, false, UdpAddress::from_ipv4({198, 18, 0, 1}, 1)),
                now);
    }

    void punch(TimePoint now) {
        deliver(test::punch(tx_id_, kRendezvousId, kHostPeer, RendezvousRole::kHost,
                            {kHostReflexive, kHostMapped}),
                now);
    }

    // 時刻を 10ms ずつ進めながら poll() を呼ぶ。
    void run_until(TimePoint end) {
        for (TimePoint t = now_; t <= end; t += 10ms) {
            client_.poll(t);
            now_ = t;
        }
    }

    const std::vector<std::uint8_t> kInitiation = {1, 1, 0, 0, 0, 7};
    FakeUdpEndpoint endpoint_;
    transport::FakeRandomSource random_;
    RecordingListener listener_;
    ClientRendezvous client_{endpoint_, random_, listener_};
    wire::TxId tx_id_{};
    TimePoint now_{};
};

TEST_F(ClientRendezvousTest, SendsInitiationToAllHostCandidatesOnPunch) {
    registered(TimePoint{} + 20ms);
    punch(TimePoint{} + 30ms);
    EXPECT_EQ(endpoint_.count_to(kHostReflexive), 1u);
    EXPECT_EQ(endpoint_.count_to(kHostMapped), 1u);
    EXPECT_EQ(endpoint_.sent.back().datagram, kInitiation);
    run_until(TimePoint{} + 130ms);
    EXPECT_EQ(endpoint_.count_to(kHostReflexive), 3u);
}

TEST_F(ClientRendezvousTest, StopsAfterEstablished) {
    registered(TimePoint{} + 20ms);
    punch(TimePoint{} + 30ms);
    client_.mark_established();
    const auto sent = endpoint_.sent.size();
    run_until(TimePoint{} + 12s);
    EXPECT_EQ(endpoint_.sent.size(), sent);
    EXPECT_TRUE(listener_.failures.empty());
    EXPECT_FALSE(client_.next_deadline());
}

TEST_F(ClientRendezvousTest, FailsWithRendezvousUnreachableAfterFiveSeconds) {
    run_until(TimePoint{} + 4990ms);
    EXPECT_TRUE(listener_.failures.empty());
    EXPECT_EQ(endpoint_.count_to(kServer), 5u);  // 0、1、2、3、4 秒
    run_until(TimePoint{} + 5s);
    EXPECT_EQ(listener_.failures, std::vector{NetworkError::kRendezvousUnreachable});
    EXPECT_FALSE(client_.active());
}

TEST_F(ClientRendezvousTest, FailsWithHostNotFoundWhenNoPunchArrives) {
    registered(TimePoint{} + 20ms);
    run_until(TimePoint{} + 10s);
    EXPECT_EQ(listener_.failures, std::vector{NetworkError::kHostNotFound});
}

TEST_F(ClientRendezvousTest, FailsWithUnreachableWhenPunchDoesNotEstablish) {
    registered(TimePoint{} + 20ms);
    punch(TimePoint{} + 30ms);
    run_until(TimePoint{} + 10s);
    EXPECT_EQ(listener_.failures, std::vector{NetworkError::kUnreachable});
}

TEST_F(ClientRendezvousTest, FailsImmediatelyWhenServerIsFull) {
    deliver(test::registered(tx_id_, true, false, UdpAddress::from_ipv4({198, 18, 0, 1}, 1)),
            TimePoint{} + 20ms);
    EXPECT_EQ(listener_.failures, std::vector{NetworkError::kRendezvousCapacity});
    EXPECT_FALSE(client_.active());
}

TEST_F(ClientRendezvousTest, NextDeadlineCoversRetriesAndTimeouts) {
    EXPECT_EQ(client_.next_deadline(), TimePoint{} + 1s);
    registered(TimePoint{} + 20ms);
    punch(TimePoint{} + 30ms);
    EXPECT_EQ(client_.next_deadline(), TimePoint{} + 80ms);
}

}  // namespace
}  // namespace vocroz::network
