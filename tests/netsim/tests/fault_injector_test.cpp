// SPDX-License-Identifier: AGPL-3.0-only
//
// FaultInjector の単体テスト。
// 同じシードなら同じ決定の列になること（Windows と Linux で同じ条件を再現できること）と、
// 確率と遅延の範囲が設定どおりであることを確かめる。

#include "vocroz/netsim/fault_injector.h"

#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::netsim {
namespace {

using std::chrono::microseconds;
using std::chrono::milliseconds;

FaultProfile lossy_profile(std::uint64_t seed) {
    FaultProfile profile;
    profile.delay = milliseconds{20};
    profile.jitter = milliseconds{5};
    profile.loss = 0.1;
    profile.duplicate = 0.05;
    profile.reorder = 0.05;
    profile.corrupt = 0.02;
    profile.seed = seed;
    return profile;
}

std::vector<std::int64_t> first_delays(FaultInjector injector, int count) {
    std::vector<std::int64_t> delays;
    for (int i = 0; i < count; ++i) {
        const auto decision = injector.decide();
        delays.push_back(decision.count == 0 ? -1 : decision.deliveries[0].delay.count());
    }
    return delays;
}

TEST(FaultInjectorTest, NoFaultsDeliversEveryPacketOnceWithoutDelay) {
    FaultInjector injector(FaultProfile{});
    for (int i = 0; i < 1000; ++i) {
        const auto decision = injector.decide();
        ASSERT_EQ(decision.count, 1U);
        EXPECT_EQ(decision.deliveries[0].delay, microseconds{0});
        EXPECT_FALSE(decision.deliveries[0].corrupt);
    }
}

TEST(FaultInjectorTest, SameSeedGivesSameSequence) {
    EXPECT_EQ(first_delays(FaultInjector(lossy_profile(7)), 500),
              first_delays(FaultInjector(lossy_profile(7)), 500));
    EXPECT_NE(first_delays(FaultInjector(lossy_profile(7)), 500),
              first_delays(FaultInjector(lossy_profile(8)), 500));
}

TEST(FaultInjectorTest, SequenceIsStableAcrossPlatforms) {
    // 生成器と変換を自前で固定しているので、どの標準ライブラリでも同じ値になる。
    // この値が変わったら、記録済みの試験条件を再現できなくなっている。
    DeterministicRandom random(1);
    EXPECT_EQ(random.next_u64(), 0x910A2DEC89025CC1ULL);
    EXPECT_EQ(random.next_u64(), 0xBEEB8DA1658EEC67ULL);
}

TEST(FaultInjectorTest, RatesAndDelaysFollowProfile) {
    FaultInjector injector(lossy_profile(3));
    constexpr int kPackets = 100'000;
    int lost = 0;
    int duplicated = 0;
    int corrupted = 0;
    for (int i = 0; i < kPackets; ++i) {
        const auto decision = injector.decide();
        lost += decision.lost ? 1 : 0;
        duplicated += decision.duplicated ? 1 : 0;
        for (std::size_t d = 0; d < decision.count; ++d) {
            const auto& delivery = decision.deliveries[d];
            corrupted += delivery.corrupt ? 1 : 0;
            EXPECT_GE(delivery.delay, milliseconds{20});
            EXPECT_LT(delivery.delay, milliseconds{25} + injector.profile().reorder_delay);
        }
    }
    EXPECT_NEAR(lost / static_cast<double>(kPackets), 0.1, 0.01);
    EXPECT_NEAR(duplicated / static_cast<double>(kPackets), 0.05 * 0.9, 0.01);
    EXPECT_NEAR(corrupted / static_cast<double>(kPackets), 0.02 * 0.9, 0.005);
}

TEST(FaultInjectorTest, CorruptPacketFlipsExactlyOneBit) {
    std::array<std::uint8_t, 16> packet{};
    corrupt_packet(packet, 0x1234567);
    int bits = 0;
    for (const auto byte : packet) {
        bits += std::popcount(byte);
    }
    EXPECT_EQ(bits, 1);
}

}  // namespace
}  // namespace vocroz::netsim
