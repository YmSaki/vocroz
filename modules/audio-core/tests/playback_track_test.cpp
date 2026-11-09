// SPDX-License-Identifier: AGPL-3.0-only
//
// PlaybackTrack の単体テスト。ディスク I/O スレッド側の fill() とオーディオスレッド側の render() を
// 同じスレッドで交互に呼び、初期ミュート、フェード、一時停止、シーク、ファイルの終わり、
// 先読み切れの扱いを決定的に検証する。

#include "vocroz/audio_core/playback_track.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/audio_core/constants.h"

#include "fakes/fake_audio_file_reader.h"

namespace vocroz::audio_core {
namespace {

// フェード（10ms = 480 サンプル）が確実に終わるブロック数。
constexpr std::size_t kBlocksAfterFade = 4;

class PlaybackTrackTest : public ::testing::Test {
protected:
    // 1 ブロックを再生し、L と R を返す。
    std::array<std::vector<float>, 2> render_block() {
        std::array<std::vector<float>, 2> out{std::vector<float>(kBlockSize, 1.0F),
                                              std::vector<float>(kBlockSize, 1.0F)};
        const std::array<float*, 2> channels{out[0].data(), out[1].data()};
        track_.render(channels.data(), kBlockSize);
        return out;
    }

    void fill() {
        ASSERT_TRUE(track_.fill(reader_).has_value());
    }

    FakeAudioFileReader reader_{2, 10 * kSampleRate};
    PlaybackTrack track_{2, kSampleRate / 10};
};

TEST_F(PlaybackTrackTest, StaysSilentUntilPlay) {
    fill();
    for (int i = 0; i < 10; ++i) {
        const auto out = render_block();
        for (const float sample : out[0]) {
            EXPECT_EQ(sample, 0.0F);
        }
    }
    EXPECT_FALSE(track_.is_playing());
    EXPECT_EQ(track_.position_frames(), 0U);
    EXPECT_EQ(track_.underrun_frames(), 0U);
}

TEST_F(PlaybackTrackTest, FadesInThenPlaysFileSamplesInOrder) {
    fill();
    track_.play();
    const auto first = render_block();
    EXPECT_EQ(first[0][0], 0.0F);  // フェードインは 0 から始まる
    EXPECT_GT(first[0][kBlockSize - 1], 0.0F);
    for (std::size_t i = 1; i < kBlocksAfterFade; ++i) {
        (void)render_block();
    }
    const std::uint64_t start = track_.position_frames();
    EXPECT_EQ(start, kBlocksAfterFade * kBlockSize);
    const auto out = render_block();
    for (std::size_t i = 0; i < kBlockSize; ++i) {
        EXPECT_FLOAT_EQ(out[0][i], FakeAudioFileReader::sample_at(start + i, 0));
        EXPECT_FLOAT_EQ(out[1][i], FakeAudioFileReader::sample_at(start + i, 1));
    }
}

TEST_F(PlaybackTrackTest, PauseFadesOutAndKeepsPosition) {
    fill();
    track_.play();
    for (std::size_t i = 0; i < kBlocksAfterFade; ++i) {
        (void)render_block();
    }
    track_.pause();
    const auto fading = render_block();
    EXPECT_GT(fading[0][0], 0.0F);  // 急に止めずにフェードアウトする
    for (std::size_t i = 0; i < kBlocksAfterFade; ++i) {
        (void)render_block();
    }
    const std::uint64_t paused_at = track_.position_frames();
    for (int i = 0; i < 10; ++i) {
        const auto out = render_block();
        EXPECT_EQ(out[0][0], 0.0F);
    }
    EXPECT_EQ(track_.position_frames(), paused_at);

    // 再開すると、止めた位置の続きから鳴る。
    track_.play();
    for (std::size_t i = 0; i < kBlocksAfterFade; ++i) {
        (void)render_block();
    }
    const std::uint64_t resumed = track_.position_frames();
    EXPECT_EQ(resumed, paused_at + kBlocksAfterFade * kBlockSize);
    const auto out = render_block();
    EXPECT_FLOAT_EQ(out[0][0], FakeAudioFileReader::sample_at(resumed, 0));
}

TEST_F(PlaybackTrackTest, SeekWhilePausedStartsFromNewPosition) {
    fill();
    for (int i = 0; i < 2; ++i) {
        (void)render_block();
    }
    track_.seek(5 * kSampleRate);
    fill();
    (void)render_block();  // 停止中なので、すぐに古い先読みを捨てて新しい区間へ移る
    EXPECT_EQ(track_.position_frames(), 5 * kSampleRate);
    fill();  // 古い先読みが消えて空いたところへ、新しい区間を読む
    track_.play();
    for (std::size_t i = 0; i < kBlocksAfterFade; ++i) {
        (void)render_block();
    }
    const std::uint64_t position = track_.position_frames();
    const auto out = render_block();
    EXPECT_FLOAT_EQ(out[0][0], FakeAudioFileReader::sample_at(position, 0));
    EXPECT_EQ(position, 5 * kSampleRate + kBlocksAfterFade * kBlockSize);
}

TEST_F(PlaybackTrackTest, SeekWhilePlayingFadesOutBeforeJumping) {
    fill();
    track_.play();
    for (std::size_t i = 0; i < kBlocksAfterFade; ++i) {
        (void)render_block();
    }
    track_.seek(kSampleRate);
    fill();
    const auto fading = render_block();
    EXPECT_GT(fading[0][0], 0.0F);  // 移る前にフェードアウトする
    EXPECT_LT(track_.position_frames(), kSampleRate);
    // ディスク I/O スレッドの役として、ブロックごとに先読みを足す。
    for (std::size_t i = 0; i < 2 * kBlocksAfterFade; ++i) {
        (void)render_block();
        fill();
    }
    const std::uint64_t position = track_.position_frames();
    EXPECT_GT(position, kSampleRate);
    const auto out = render_block();
    EXPECT_FLOAT_EQ(out[0][0], FakeAudioFileReader::sample_at(position, 0));
}

TEST_F(PlaybackTrackTest, ReportsEndWithoutCountingUnderrun) {
    FakeAudioFileReader short_file(2, 300);
    PlaybackTrack track(2, 1024);
    ASSERT_TRUE(track.fill(short_file).has_value());
    ASSERT_TRUE(track.fill(short_file).has_value());  // ここでファイルの終わりを知る
    track.play();
    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};
    const std::array<float*, 2> channels{left.data(), right.data()};
    for (int i = 0; i < 4; ++i) {
        track.render(channels.data(), kBlockSize);
    }
    EXPECT_TRUE(track.at_end());
    EXPECT_EQ(track.position_frames(), 300U);
    EXPECT_EQ(track.underrun_frames(), 0U);
    EXPECT_EQ(left[0], 0.0F);

    // 先頭へシークすれば、もう一度鳴らせる。
    track.seek(0);
    ASSERT_TRUE(track.fill(short_file).has_value());
    EXPECT_FALSE(track.at_end());
}

TEST_F(PlaybackTrackTest, FillsUnderrunWithSilenceAndCountsIt) {
    reader_.limit_frames_per_read(kBlockSize);
    PlaybackTrack track(2, kBlockSize);  // 1 ブロックぶんしか先読みしない
    ASSERT_TRUE(track.fill(reader_).has_value());
    track.play();
    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};
    const std::array<float*, 2> channels{left.data(), right.data()};
    track.render(channels.data(), kBlockSize);
    track.render(channels.data(), kBlockSize);  // fill() が間に合わなかった
    EXPECT_EQ(track.underrun_frames(), kBlockSize);
    EXPECT_EQ(left[0], 0.0F);
    EXPECT_EQ(track.position_frames(), kBlockSize);
}

TEST_F(PlaybackTrackTest, ReturnsReadErrorAndKeepsPrefetchedAudio) {
    fill();
    reader_.fail_next_read(AudioFileError::kIoError);
    track_.play();
    for (std::size_t i = 0; i < kBlocksAfterFade; ++i) {
        (void)render_block();
    }
    EXPECT_EQ(track_.fill(reader_).error(), AudioFileError::kIoError);
    const auto out = render_block();
    EXPECT_GT(out[0][0], 0.0F);
}

TEST_F(PlaybackTrackTest, PlaysMonoFile) {
    FakeAudioFileReader mono(1, kSampleRate);
    PlaybackTrack track(1, 4096);
    ASSERT_TRUE(track.fill(mono).has_value());
    track.play();
    std::array<float, kBlockSize> out{};
    const std::array<float*, 1> channels{out.data()};
    for (std::size_t i = 0; i <= kBlocksAfterFade; ++i) {
        track.render(channels.data(), kBlockSize);
    }
    EXPECT_FLOAT_EQ(out[0], FakeAudioFileReader::sample_at(kBlocksAfterFade * kBlockSize, 0));
}

}  // namespace
}  // namespace vocroz::audio_core
