// SPDX-License-Identifier: AGPL-3.0-only
//
// 返送の再生（ReturnPlayer）のテスト。返送が届いていない間は無音を書くこと、モノラルの返送を
// デバイスのすべてのチャンネルに書くこと、作業領域より長いコールバックも埋めること、抜去の印を
// 1 回だけ取り出せることを確かめる。

#include "return_player.h"

#include <cstddef>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/transport/media_frame.h"

#include "return_frames.h"

namespace vocroz::client {
namespace {

namespace tp = transport;

class ReturnPlayerTest : public ::testing::Test {
protected:
    void enqueue_frames(int count) {
        const auto opus = test::encode_return_frame();
        for (int i = 0; i < count; ++i) {
            const auto frame =
                tp::make_media_frame(static_cast<std::uint64_t>(i) * test::kReturnFrameSamples, 1,
                                     opus)
                    .value();
            ASSERT_TRUE(returns_->enqueue(frame));
        }
    }

    std::unique_ptr<tp::PlayoutStream> returns_ =
        tp::PlayoutStream::create(tp::PlayoutConfig{}).value();
    ReturnPlayer player_{*returns_};
};

TEST_F(ReturnPlayerTest, WritesSilenceWhileNothingHasArrived) {
    std::vector<float> out(256 * 2, 1.0F);
    player_.render(audio_core::InterleavedBuffer{out, 2});
    for (const float sample : out) {
        EXPECT_EQ(sample, 0.0F);
    }
}

TEST_F(ReturnPlayerTest, WritesTheMonoReturnToEveryChannel) {
    enqueue_frames(16);
    std::vector<float> out(480 * 2);
    bool heard = false;
    for (int i = 0; i < 8; ++i) {
        player_.render(audio_core::InterleavedBuffer{out, 2});
        for (std::size_t f = 0; f < 480; ++f) {
            EXPECT_EQ(out[f * 2], out[(f * 2) + 1]);
            heard = heard || out[f * 2] != 0.0F;
        }
    }
    EXPECT_TRUE(heard);
}

TEST_F(ReturnPlayerTest, FillsCallbacksLongerThanItsWorkArea) {
    const std::size_t frames = kReturnRenderChunkFrames * 2 + 7;
    std::vector<float> out(frames * 3, 1.0F);
    player_.render(audio_core::InterleavedBuffer{out, 3});
    for (const float sample : out) {
        EXPECT_EQ(sample, 0.0F);
    }
}

TEST_F(ReturnPlayerTest, DeviceLossIsTakenOnce) {
    EXPECT_FALSE(player_.take_device_lost());
    player_.on_device_lost();
    EXPECT_TRUE(player_.take_device_lost());
    EXPECT_FALSE(player_.take_device_lost());
}

}  // namespace
}  // namespace vocroz::client
