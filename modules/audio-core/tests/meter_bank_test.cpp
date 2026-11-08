// SPDX-License-Identifier: AGPL-3.0-only
//
// MeterBank の単体テスト。ピークの保持とリセット、RMS の収束を検証する。

#include "meter_bank.h"

#include <array>
#include <cmath>

#include <gtest/gtest.h>

#include "vocroz/audio_core/constants.h"

namespace vocroz::audio_core {
namespace {

void measure_constant(MeterBank& bank, MeterId meter, float value, std::size_t blocks) {
    std::array<float, kBlockSize> samples{};
    samples.fill(value);
    std::array<const float*, 1> channels{samples.data()};
    for (std::size_t i = 0; i < blocks; ++i) {
        bank.measure(meter, channels.data(), 1, kBlockSize);
    }
}

TEST(MeterBankTest, HoldsPeakUntilReadThenResets) {
    MeterBank bank(1);
    const MeterId meter{0};
    measure_constant(bank, meter, -0.8F, 1);
    measure_constant(bank, meter, 0.2F, 1);
    EXPECT_FLOAT_EQ(bank.read(meter).peak, 0.8F);
    EXPECT_EQ(bank.read(meter).peak, 0.0F);
}

TEST(MeterBankTest, RmsConvergesToSignalLevel) {
    MeterBank bank(1);
    const MeterId meter{0};
    // 時定数 300ms の 5 倍（1.5 秒）ぶん測れば、ほぼ収束する。
    measure_constant(bank, meter, 0.5F, kSampleRate * 3 / 2 / kBlockSize);
    EXPECT_NEAR(bank.read(meter).rms, 0.5F, 0.01F);
}

TEST(MeterBankTest, RmsDecaysAfterSignalStops) {
    MeterBank bank(1);
    const MeterId meter{0};
    measure_constant(bank, meter, 0.5F, kSampleRate * 3 / 2 / kBlockSize);
    // 二乗平均が時定数で減衰するので、RMS はその平方根で減る。3 秒（時定数の 10 倍）で 1%
    // 未満になる。
    measure_constant(bank, meter, 0.0F, kSampleRate * 3 / kBlockSize);
    EXPECT_LT(bank.read(meter).rms, 0.01F);
}

TEST(MeterBankTest, ReturnsZeroForOutOfRangeMeter) {
    MeterBank bank(1);
    measure_constant(bank, MeterId{3}, 1.0F, 1);
    const MeterReading reading = bank.read(MeterId{3});
    EXPECT_EQ(reading.peak, 0.0F);
    EXPECT_EQ(reading.rms, 0.0F);
}

}  // namespace
}  // namespace vocroz::audio_core
