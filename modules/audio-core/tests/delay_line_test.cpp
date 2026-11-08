// SPDX-License-Identifier: AGPL-3.0-only
//
// DelayLine の単体テスト。その場で遅らせる使い方と、読みと書きを分ける使い方の両方で、
// 決まった数だけ遅れることを検証する。

#include "delay_line.h"

#include <numeric>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::audio_core {
namespace {

std::vector<float> ramp(std::size_t count, float start) {
    std::vector<float> samples(count);
    std::iota(samples.begin(), samples.end(), start);
    return samples;
}

TEST(DelayLineTest, ZeroDelayLeavesSamplesUnchanged) {
    DelayLine line(0);
    std::vector<float> samples = ramp(8, 1.0F);
    line.process(samples);
    EXPECT_EQ(samples, ramp(8, 1.0F));
}

TEST(DelayLineTest, DelaysInPlaceAcrossBlocksShorterThanDelay) {
    DelayLine line(5);
    std::vector<float> first = ramp(3, 1.0F);   // 1 2 3
    std::vector<float> second = ramp(3, 4.0F);  // 4 5 6
    std::vector<float> third = ramp(3, 7.0F);   // 7 8 9
    line.process(first);
    line.process(second);
    line.process(third);
    EXPECT_EQ(first, (std::vector<float>{0.0F, 0.0F, 0.0F}));
    EXPECT_EQ(second, (std::vector<float>{0.0F, 0.0F, 1.0F}));
    EXPECT_EQ(third, (std::vector<float>{2.0F, 3.0F, 4.0F}));
}

TEST(DelayLineTest, DelaysInPlaceWithinOneBlockLongerThanDelay) {
    DelayLine line(2);
    std::vector<float> samples = ramp(6, 1.0F);
    line.process(samples);
    EXPECT_EQ(samples, (std::vector<float>{0.0F, 0.0F, 1.0F, 2.0F, 3.0F, 4.0F}));
}

TEST(DelayLineTest, ReadThenWriteDelaysByTheSameAmountAsProcess) {
    DelayLine split(4);
    DelayLine in_place(4);
    for (int block = 0; block < 4; ++block) {
        const std::vector<float> input = ramp(4, static_cast<float>(block * 4 + 1));
        std::vector<float> read(4);
        split.read(read);
        split.write(input);
        std::vector<float> expected = input;
        in_place.process(expected);
        EXPECT_EQ(read, expected) << "block " << block;
    }
}

TEST(DelayLineTest, ReadsSilenceBeyondDelay) {
    DelayLine line(2);
    std::vector<float> out(4, 1.0F);
    line.read(out);
    EXPECT_EQ(out, (std::vector<float>{0.0F, 0.0F, 0.0F, 0.0F}));
}

}  // namespace
}  // namespace vocroz::audio_core
