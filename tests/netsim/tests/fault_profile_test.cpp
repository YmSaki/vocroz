// SPDX-License-Identifier: AGPL-3.0-only
//
// parse_fault_profile() と format_fault_profile() の単体テスト。
// CLI とテストで同じ書式を使うので、読み戻しで同じ設定になることと、誤りを拒否することを確かめる。

#include "vocroz/netsim/fault_profile.h"

#include <chrono>

#include <gtest/gtest.h>

namespace vocroz::netsim {
namespace {

using std::chrono::microseconds;
using std::chrono::milliseconds;

TEST(FaultProfileTest, EmptyTextMeansNoFaults) {
    const auto profile = parse_fault_profile("");
    ASSERT_TRUE(profile);
    EXPECT_EQ(profile.value().delay, microseconds{0});
    EXPECT_EQ(profile.value().loss, 0.0);
}

TEST(FaultProfileTest, ParsesAllKeysAndUnits) {
    const auto profile = parse_fault_profile(
        "delay=20ms,jitter=500us,loss=0.05,duplicate=0.01,reorder=0.02,reorder_delay=1s,"
        "corrupt=0.5,seed=42");
    ASSERT_TRUE(profile);
    EXPECT_EQ(profile.value().delay, milliseconds{20});
    EXPECT_EQ(profile.value().jitter, microseconds{500});
    EXPECT_DOUBLE_EQ(profile.value().loss, 0.05);
    EXPECT_DOUBLE_EQ(profile.value().duplicate, 0.01);
    EXPECT_DOUBLE_EQ(profile.value().reorder, 0.02);
    EXPECT_EQ(profile.value().reorder_delay, std::chrono::seconds{1});
    EXPECT_DOUBLE_EQ(profile.value().corrupt, 0.5);
    EXPECT_EQ(profile.value().seed, 42U);
}

TEST(FaultProfileTest, FormatRoundTrips) {
    FaultProfile original;
    original.delay = milliseconds{7};
    original.jitter = microseconds{1234};
    original.loss = 0.125;
    original.seed = 99;
    const auto parsed = parse_fault_profile(format_fault_profile(original));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value().delay, original.delay);
    EXPECT_EQ(parsed.value().jitter, original.jitter);
    EXPECT_DOUBLE_EQ(parsed.value().loss, original.loss);
    EXPECT_EQ(parsed.value().seed, original.seed);
}

TEST(FaultProfileTest, RejectsUnknownKeyBadUnitAndOutOfRangeProbability) {
    EXPECT_FALSE(parse_fault_profile("latency=1ms"));
    EXPECT_FALSE(parse_fault_profile("delay=1"));
    EXPECT_FALSE(parse_fault_profile("delay=1.5ms"));
    EXPECT_FALSE(parse_fault_profile("loss=1.5"));
    EXPECT_FALSE(parse_fault_profile("loss"));
    EXPECT_FALSE(parse_fault_profile("seed=-1"));
}

}  // namespace
}  // namespace vocroz::netsim
