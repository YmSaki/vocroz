// SPDX-License-Identifier: AGPL-3.0-only
//
// WASAPI のループバック（factories.h）の単体テスト。Windows でだけビルドする。
// 再生デバイスがない環境（CI のランナー）では、デバイスが要るテストをスキップする。

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "vocroz/capture/factories.h"

#include "fakes/fake_logger.h"

namespace vocroz::capture {
namespace {

using audio_core::AudioDeviceError;

class CountingCapture final : public audio_core::AudioCaptureCallback {
public:
    void on_captured(audio_core::ConstInterleavedBuffer in) noexcept VOCROZ_NONBLOCKING override {
        frames.fetch_add(in.frames(), std::memory_order_relaxed);
        if (in.channels != 2) {
            wrong_channels.store(true, std::memory_order_relaxed);
        }
    }
    std::atomic<std::size_t> frames{0};
    std::atomic<bool> wrong_channels{false};
};

class CountingListener final : public audio_core::AudioDeviceListener {
public:
    void on_device_lost() noexcept override {
        lost.fetch_add(1, std::memory_order_relaxed);
    }
    std::atomic<int> lost{0};
};

// ストリームが数周期ぶん動くのを待つ時間。エンジンの周期（10ms 程度）より十分長くする。
constexpr auto kRunTime = std::chrono::milliseconds(300);

TEST(WasapiLoopbackInputTest, IsAlwaysStereo) {
    common::FakeLogger logger;
    auto input = make_wasapi_loopback_input(logger);
    ASSERT_TRUE(input.has_value());
    EXPECT_EQ(input.value()->channel_count(), 2U);
    input.value()->stop();  // 開始前に止めてもよい
}

TEST(WasapiLoopbackInputTest, DeliversStereoFramesEvenWhileNothingIsPlaying) {
    common::FakeLogger logger;
    auto input = make_wasapi_loopback_input(logger);
    ASSERT_TRUE(input.has_value());
    CountingCapture capture;
    CountingListener listener;
    const auto started = input.value()->start(capture, listener);
    if (!started && started.error() == AudioDeviceError::kDeviceNotFound) {
        GTEST_SKIP() << "no render device";
    }
    ASSERT_TRUE(started.has_value());
    // 二重の開始は断る。
    const auto again = input.value()->start(capture, listener);
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error(), AudioDeviceError::kAlreadyStarted);

    std::this_thread::sleep_for(kRunTime);
    input.value()->stop();
    // テストは何も再生しないので、届くのは無音を流し続ける再生ストリームによる無音のパケット。
    EXPECT_GT(capture.frames.load(), 0U);
    EXPECT_FALSE(capture.wrong_channels.load());
    EXPECT_EQ(listener.lost.load(), 0);

    // 止めた後は開き直せる。
    const std::size_t before = capture.frames.load();
    ASSERT_TRUE(input.value()->start(capture, listener).has_value());
    std::this_thread::sleep_for(kRunTime);
    input.value()->stop();
    EXPECT_GT(capture.frames.load(), before);
}

}  // namespace
}  // namespace vocroz::capture
