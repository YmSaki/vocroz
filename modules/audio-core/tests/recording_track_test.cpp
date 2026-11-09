// SPDX-License-Identifier: AGPL-3.0-only
//
// RecordingTrack の単体テスト。オーディオスレッド側の push() とディスク I/O スレッド側の drain() を
// 同じスレッドで交互に呼び、書き出しの内容、溢れたときの欠落と無音での穴埋め、容量不足の扱いを
// 決定的に検証する。別スレッドでの書き込みの遅延は recording_and_playback_test.cpp で扱う。

#include "vocroz/audio_core/recording_track.h"

#include <array>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_audio_file_writer.h"

namespace vocroz::audio_core {
namespace {

// 値 value が並んだ 1 チャンネルのブロックを push する。
void push_constant(RecordingTrack& track, float value, std::size_t frames) {
    std::vector<float> block(frames, value);
    const std::array<const float*, 1> channels{block.data()};
    track.push(channels.data(), frames);
}

TEST(RecordingTrackTest, WritesStereoBlocksInterleaved) {
    RecordingTrack track(2, 64);
    FakeAudioFileWriter writer(2);
    const std::array<float, 2> left{0.1F, 0.2F};
    const std::array<float, 2> right{-0.1F, -0.2F};
    const std::array<const float*, 2> channels{left.data(), right.data()};
    track.push(channels.data(), 2);
    ASSERT_EQ(track.drain(writer).value(), 2U);
    EXPECT_EQ(writer.samples(), (std::vector<float>{0.1F, -0.1F, 0.2F, -0.2F}));
    EXPECT_EQ(track.dropped_frames(), 0U);
}

TEST(RecordingTrackTest, FillsOverflowWithSilenceToKeepTimeline) {
    // 4 ブロックぶんしか入らないところに 6 ブロック書くと、後の 2 ブロックが欠落する。
    // 欠落を無音で埋めるので、ファイルの長さは書いたブロック数と一致し、後続のサンプルの位置もずれない。
    RecordingTrack track(1, 32);
    FakeAudioFileWriter writer(1);
    for (int i = 0; i < 6; ++i) {
        push_constant(track, static_cast<float>(i + 1), 8);
    }
    EXPECT_EQ(track.dropped_frames(), 16U);
    ASSERT_TRUE(track.drain(writer).has_value());
    push_constant(track, 7.0F, 8);
    ASSERT_TRUE(track.drain(writer).has_value());

    const std::vector<float> samples = writer.samples();
    ASSERT_EQ(samples.size(), 7U * 8U);
    const std::array<float, 7> expected{1, 2, 3, 4, 0, 0, 7};
    for (std::size_t block = 0; block < expected.size(); ++block) {
        for (std::size_t i = 0; i < 8; ++i) {
            EXPECT_EQ(samples[block * 8 + i], expected[block]) << "block " << block;
        }
    }
}

TEST(RecordingTrackTest, KeepsDroppingWhileDiskIsBehind) {
    // 空きができても、欠落の記録を渡すまでは書かない。欠落が続けば 1 件にまとまる。
    RecordingTrack track(1, 16);
    FakeAudioFileWriter writer(1);
    push_constant(track, 1.0F, 16);
    push_constant(track, 2.0F, 8);
    push_constant(track, 3.0F, 8);
    ASSERT_TRUE(track.drain(writer).has_value());
    push_constant(track, 4.0F, 8);
    ASSERT_TRUE(track.drain(writer).has_value());
    const std::vector<float> samples = writer.samples();
    ASSERT_EQ(samples.size(), 40U);
    EXPECT_EQ(samples[15], 1.0F);
    EXPECT_EQ(samples[16], 0.0F);
    EXPECT_EQ(samples[31], 0.0F);
    EXPECT_EQ(samples[32], 4.0F);
}

TEST(RecordingTrackTest, FinishFillsTrailingGapAndClosesFile) {
    // 最後まで欠落が続いたまま止めても、欠落ぶんの無音を書いてから閉じる。
    RecordingTrack track(1, 8);
    FakeAudioFileWriter writer(1);
    push_constant(track, 1.0F, 8);
    push_constant(track, 2.0F, 8);
    push_constant(track, 3.0F, 8);
    ASSERT_EQ(track.finish(writer).value(), 24U);
    EXPECT_TRUE(writer.finished());
    const std::vector<float> samples = writer.samples();
    ASSERT_EQ(samples.size(), 24U);
    EXPECT_EQ(samples[7], 1.0F);
    EXPECT_EQ(samples[8], 0.0F);
    EXPECT_EQ(samples[23], 0.0F);
}

TEST(RecordingTrackTest, StopsWritingAfterDiskFull) {
    RecordingTrack track(1, 64);
    FakeAudioFileWriter writer(1);
    writer.set_capacity_frames(8);
    push_constant(track, 1.0F, 8);
    ASSERT_TRUE(track.drain(writer).has_value());
    push_constant(track, 2.0F, 8);
    EXPECT_EQ(track.drain(writer).error(), AudioFileError::kDiskFull);
    EXPECT_EQ(track.failure(), AudioFileError::kDiskFull);

    // 失敗後の push() は何もせず、drain() は同じエラーを返し続ける。
    push_constant(track, 3.0F, 8);
    EXPECT_EQ(track.drain(writer).error(), AudioFileError::kDiskFull);
    EXPECT_EQ(writer.frames(), 8U);
    EXPECT_EQ(track.dropped_frames(), 0U);
}

TEST(RecordingTrackTest, SplitsLargeBacklogIntoChunks) {
    // 1 回の write() に渡す量は作業領域の大きさまで。溜まったぶんは 1 回の drain() ですべて書く。
    RecordingTrack track(1, 16'384);
    FakeAudioFileWriter writer(1);
    for (int i = 0; i < 100; ++i) {
        push_constant(track, 0.5F, 128);
    }
    EXPECT_EQ(track.drain(writer).value(), 12'800U);
    EXPECT_EQ(writer.frames(), 12'800U);
}

TEST(RecordingTrackTest, ClampsChannelCount) {
    EXPECT_EQ(RecordingTrack(0).channel_count(), 1U);
    EXPECT_EQ(RecordingTrack(3).channel_count(), 2U);
}

}  // namespace
}  // namespace vocroz::audio_core
