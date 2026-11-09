// SPDX-License-Identifier: AGPL-3.0-only
//
// SampleRingBuffer の単体テスト。planar とインターリーブの変換、領域の折り返し、
// 全部か無しの書き込み、読み飛ばしの範囲を検証する。

#include "sample_ring_buffer.h"

#include <array>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::audio_core {
namespace {

TEST(SampleRingBufferTest, ConvertsPlanarToInterleavedAcrossWrapAround) {
    SampleRingBuffer ring(2, 5);
    std::vector<float> scratch(10, 0.0F);
    // 3 フレーム書いて読み、書き込み位置を領域の途中に進めておく。
    const std::array<float, 3> l1{1, 2, 3};
    const std::array<float, 3> r1{-1, -2, -3};
    const std::array<const float*, 2> first{l1.data(), r1.data()};
    ASSERT_TRUE(ring.write_planar(first.data(), 3));
    EXPECT_EQ(ring.read_interleaved(scratch, 3), 3U);

    const std::array<float, 4> l2{4, 5, 6, 7};
    const std::array<float, 4> r2{-4, -5, -6, -7};
    const std::array<const float*, 2> second{l2.data(), r2.data()};
    ASSERT_TRUE(ring.write_planar(second.data(), 4));  // 領域の終わりで折り返す
    ASSERT_EQ(ring.read_interleaved(scratch, 10), 4U);
    EXPECT_EQ(scratch[0], 4.0F);
    EXPECT_EQ(scratch[1], -4.0F);
    EXPECT_EQ(scratch[6], 7.0F);
    EXPECT_EQ(scratch[7], -7.0F);
    EXPECT_EQ(ring.read_position(), 7U);
    EXPECT_EQ(ring.write_position(), 7U);
}

TEST(SampleRingBufferTest, ConvertsInterleavedToPlanarAcrossWrapAround) {
    SampleRingBuffer ring(2, 4);
    const std::vector<float> data{1, -1, 2, -2, 3, -3};
    EXPECT_EQ(ring.write_interleaved(data), 3U);
    std::array<float, 4> left{};
    std::array<float, 4> right{};
    const std::array<float*, 2> out{left.data(), right.data()};
    EXPECT_EQ(ring.read_planar(out.data(), 2), 2U);
    EXPECT_EQ(ring.write_interleaved(data), 3U);  // 折り返して書く
    EXPECT_EQ(ring.read_planar(out.data(), 4), 4U);
    EXPECT_EQ(left[0], 3.0F);
    EXPECT_EQ(right[0], -3.0F);
    EXPECT_EQ(left[1], 1.0F);
    EXPECT_EQ(left[3], 3.0F);
    EXPECT_EQ(right[3], -3.0F);
}

TEST(SampleRingBufferTest, PlanarWriteIsAllOrNothing) {
    SampleRingBuffer ring(1, 4);
    const std::array<float, 3> block{1, 2, 3};
    const std::array<const float*, 1> channels{block.data()};
    ASSERT_TRUE(ring.write_planar(channels.data(), 3));
    EXPECT_FALSE(ring.write_planar(channels.data(), 3));  // 空きは 1 フレームだけ
    EXPECT_EQ(ring.readable_frames(), 3U);
    EXPECT_EQ(ring.writable_frames(), 1U);
}

TEST(SampleRingBufferTest, InterleavedWriteStopsWhenFull) {
    SampleRingBuffer ring(1, 4);
    const std::vector<float> data{1, 2, 3, 4, 5, 6};
    EXPECT_EQ(ring.write_interleaved(data), 4U);
    EXPECT_EQ(ring.writable_frames(), 0U);
}

TEST(SampleRingBufferTest, SkipToClampsToWritePositionAndNeverMovesBack) {
    SampleRingBuffer ring(1, 8);
    const std::vector<float> data{1, 2, 3, 4};
    (void)ring.write_interleaved(data);
    ring.skip_to(100);
    EXPECT_EQ(ring.read_position(), 4U);
    ring.skip_to(1);
    EXPECT_EQ(ring.read_position(), 4U);
    EXPECT_EQ(ring.readable_frames(), 0U);
}

TEST(SampleRingBufferTest, ClampsChannelCountAndCapacity) {
    const SampleRingBuffer many(5, 0);
    EXPECT_EQ(many.channel_count(), 2U);
    EXPECT_EQ(many.capacity_frames(), 1U);
    const SampleRingBuffer none(0, 3);
    EXPECT_EQ(none.channel_count(), 1U);
}

}  // namespace
}  // namespace vocroz::audio_core
