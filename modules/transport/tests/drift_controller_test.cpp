// SPDX-License-Identifier: AGPL-3.0-only
//
// DriftController
// の単体テスト。深さの誤差に対する変換比の向き、上限、ドリフトの推定と保持を確かめる。

#include "drift_controller.h"

#include <gtest/gtest.h>

#include "transport_constants.h"

namespace vocroz::transport {
namespace {

constexpr std::size_t kBlock = 128;

TEST(DriftControllerTest, StartsAtUnityRatio) {
    const DriftController controller;
    EXPECT_EQ(controller.ratio(), 1.0);
}

TEST(DriftControllerTest, ConsumesFasterWhenDeeperThanTarget) {
    DriftController controller;
    EXPECT_GT(controller.update(960.0, 480.0, kBlock), 1.0);
}

TEST(DriftControllerTest, ConsumesSlowerWhenShallowerThanTarget) {
    DriftController controller;
    EXPECT_LT(controller.update(120.0, 480.0, kBlock), 1.0);
}

TEST(DriftControllerTest, ClampsRatioToMaximumDeviation) {
    DriftController controller;
    for (int i = 0; i < 100000; ++i) {
        static_cast<void>(controller.update(48000.0, 480.0, kBlock));
    }
    EXPECT_NEAR(controller.ratio(), 1.0 + kMaxRateDeviation, 1e-12);
    EXPECT_NEAR(controller.drift_estimate(), kMaxDriftEstimate, 1e-12);
}

TEST(DriftControllerTest, IntegratesConstantDriftAndKeepsItAcrossRestart) {
    // 送信側が 100ppm 速い場合を、深さの変化として模擬する。
    // 深さ = 目標 + 積算した（送信の速さ - 消費の速さ）。
    DriftController controller;
    const double target = 480.0;
    const double drift = 100e-6;
    double depth = target;
    for (std::size_t i = 0; i < 48000 / kBlock * 120; ++i) {  // 120 秒
        const double ratio = controller.update(depth, target, kBlock);
        depth += (1.0 + drift - ratio) * kBlock;
    }
    EXPECT_NEAR(controller.drift_estimate(), drift, drift * 0.1);
    EXPECT_NEAR(depth, target, 10.0);

    controller.restart();
    EXPECT_NEAR(controller.drift_estimate(), drift, drift * 0.1);
    EXPECT_NEAR(controller.ratio(), 1.0 + controller.drift_estimate(), 1e-12);
}

}  // namespace
}  // namespace vocroz::transport
