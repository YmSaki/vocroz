// SPDX-License-Identifier: AGPL-3.0-only
//
// SimulatedNetwork の単体テスト。
// 到着時刻の順に届くこと、途絶の 2 つの扱い（捨てる、溜めて後で届ける）、受信の関数の中からの送信、
// 宛先がない場合を FakeClock で確かめる。

#include "vocroz/netsim/simulated_network.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_clock.h"

namespace vocroz::netsim {
namespace {

using std::chrono::milliseconds;

constexpr NodeAddress kA{.host = 1, .port = 1000};
constexpr NodeAddress kB{.host = 2, .port = 2000};

class SimulatedNetworkTest : public ::testing::Test {
protected:
    void SetUp() override {
        network_.attach(kB, [this](const NodeAddress& from, std::span<const std::uint8_t> data) {
            EXPECT_EQ(from, kA);
            received_.push_back(data.empty() ? 0 : data[0]);
        });
    }

    void send(std::uint8_t tag) {
        const std::uint8_t data[] = {tag};
        network_.send(kA, kB, data);
    }

    void advance(milliseconds delta) {
        clock_.advance(delta);
        network_.deliver_due();
    }

    common::FakeClock clock_;
    SimulatedNetwork network_{clock_};
    std::vector<std::uint8_t> received_;
};

TEST_F(SimulatedNetworkTest, DeliversAfterDelayInArrivalOrder) {
    FaultProfile profile;
    profile.delay = milliseconds{10};
    network_.set_fault_profile(kA, kB, profile);
    send(1);
    advance(milliseconds{5});
    send(2);
    advance(milliseconds{4});
    EXPECT_TRUE(received_.empty());
    advance(milliseconds{1});
    EXPECT_EQ(received_, std::vector<std::uint8_t>{1});
    advance(milliseconds{5});
    EXPECT_EQ(received_, (std::vector<std::uint8_t>{1, 2}));
    EXPECT_EQ(network_.stats(kA, kB).delivered, 2U);
}

TEST_F(SimulatedNetworkTest, DropOutageLosesPackets) {
    network_.begin_outage(kA, kB, OutageMode::kDrop);
    send(1);
    network_.end_outage(kA, kB);
    send(2);
    advance(milliseconds{1});
    EXPECT_EQ(received_, std::vector<std::uint8_t>{2});
    EXPECT_EQ(network_.stats(kA, kB).dropped_in_outage, 1U);
}

TEST_F(SimulatedNetworkTest, HoldOutageReleasesBurstAfterOutage) {
    network_.begin_outage(kA, kB, OutageMode::kHold);
    send(1);
    send(2);
    advance(milliseconds{100});
    EXPECT_TRUE(received_.empty());
    network_.end_outage(kA, kB);
    advance(milliseconds{0});
    EXPECT_EQ(received_, (std::vector<std::uint8_t>{1, 2}));
    EXPECT_EQ(network_.stats(kA, kB).held_in_outage, 2U);
}

TEST_F(SimulatedNetworkTest, ReceiverMaySendDuringDelivery) {
    std::vector<std::uint8_t> echoes;
    network_.attach(kA, [&](const NodeAddress&, std::span<const std::uint8_t> data) {
        echoes.push_back(data[0]);
    });
    network_.attach(kB, [&](const NodeAddress& from, std::span<const std::uint8_t> data) {
        network_.send(kB, from, data);
    });
    send(7);
    advance(milliseconds{0});
    EXPECT_EQ(echoes, std::vector<std::uint8_t>{7});
}

TEST_F(SimulatedNetworkTest, CountsUndeliverableAfterDetach) {
    network_.detach(kB);
    send(1);
    advance(milliseconds{1});
    EXPECT_TRUE(received_.empty());
    EXPECT_EQ(network_.stats(kA, kB).undeliverable, 1U);
}

TEST_F(SimulatedNetworkTest, JitterReordersButDeliversEverything) {
    FaultProfile profile;
    profile.jitter = milliseconds{20};
    profile.seed = 5;
    network_.set_fault_profile(kA, kB, profile);
    for (std::uint8_t i = 0; i < 100; ++i) {
        send(i);
        advance(milliseconds{1});
    }
    advance(milliseconds{50});
    ASSERT_EQ(received_.size(), 100U);
    EXPECT_FALSE(std::is_sorted(received_.begin(), received_.end()));
    EXPECT_FALSE(network_.next_delivery_time().has_value());
}

}  // namespace
}  // namespace vocroz::netsim
