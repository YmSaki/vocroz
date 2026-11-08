// SPDX-License-Identifier: AGPL-3.0-only
//
// 端から端までの遅延の結合テスト。
// 実際の伝送の部品（Opus、暗号化、ジッターバッファ、ドリフト補正）を通した遅延が、構成から計算した
// 内訳の範囲に収まることを確かめる。範囲を超えたら、どこかに想定外のバッファが入っている
// （低遅延は必須要件で、オーディオパスに追加のバッファリングを入れない）。

#include <chrono>

#include <gtest/gtest.h>

#include "vocroz/test_harness/latency_measurement.h"

namespace vocroz::test_harness {
namespace {

using std::chrono::milliseconds;

LatencyReport measure(const LatencyScenario& scenario) {
    auto report = measure_latency(scenario);
    EXPECT_TRUE(report);
    return report ? std::move(report).value() : LatencyReport{};
}

TEST(LatencyTest, LanProfileStaysWithinBudget) {
    LatencyScenario scenario;
    scenario.link.uplink.delay = milliseconds{1};
    scenario.duration = milliseconds{8'000};
    const auto report = measure(scenario);
    SCOPED_TRACE(format_latency_report(report));
    ASSERT_GT(report.bursts_sent, 10U);
    EXPECT_EQ(report.bursts_detected, report.bursts_sent);
    EXPECT_GE(report.min_ms, report.budget.total_min_ms);
    EXPECT_LE(report.max_ms, report.budget.total_max_ms);
    // 低遅延プロファイル（ジッターバッファ 10〜20ms）の LAN では、デバイスを除いて 40ms
    // を超えない。
    EXPECT_LE(report.p95_ms, 40.0);
}

TEST(LatencyTest, WanProfileWithJitterStaysWithinBudget) {
    LatencyScenario scenario;
    scenario.link.uplink.delay = milliseconds{15};
    scenario.link.uplink.jitter = milliseconds{4};
    scenario.link.uplink.loss = 0.01;
    scenario.link.uplink.seed = 11;
    scenario.duration = milliseconds{8'000};
    const auto report = measure(scenario);
    SCOPED_TRACE(format_latency_report(report));
    ASSERT_GT(report.bursts_sent, 10U);
    // 1% の欠損でバーストの立ち上がりが欠けることがあるので、9 割以上を検出できればよい。
    EXPECT_GE(report.bursts_detected * 10, report.bursts_sent * 9);
    EXPECT_GE(report.min_ms, report.budget.total_min_ms);
    EXPECT_LE(report.max_ms, report.budget.total_max_ms);
}

TEST(LatencyTest, DeviceLatencyIsAddedToEstimate) {
    LatencyScenario scenario;
    scenario.link.uplink.delay = milliseconds{1};
    scenario.duration = milliseconds{3'000};
    const auto without_device = measure(scenario);
    scenario.device_input_latency = milliseconds{3};
    scenario.device_output_latency = milliseconds{7};
    const auto with_device = measure(scenario);
    EXPECT_NEAR(with_device.median_ms - without_device.median_ms, 10.0, 1e-6);
}

}  // namespace
}  // namespace vocroz::test_harness
