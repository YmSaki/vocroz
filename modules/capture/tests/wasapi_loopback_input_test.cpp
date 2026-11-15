// SPDX-License-Identifier: AGPL-3.0-only
//
// WASAPI のループバック（factories.h）の単体テスト。Windows でだけビルドする。
// 再生デバイスがない環境（CI のランナー）では、デバイスが要るテストをスキップする。

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

#include <gtest/gtest.h>

#include "vocroz/capture/factories.h"

#include "fakes/fake_logger.h"
#include "support/tone_player.h"

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

// 受け取ったサンプルの絶対値の最大を記録する。
class PeakCapture final : public audio_core::AudioCaptureCallback {
public:
    void on_captured(audio_core::ConstInterleavedBuffer in) noexcept VOCROZ_NONBLOCKING override {
        frames.fetch_add(in.frames(), std::memory_order_relaxed);
        float local = 0.0F;
        for (const float sample : in.samples) {
            local = std::max(local, std::fabs(sample));
        }
        float seen = peak.load(std::memory_order_relaxed);
        while (local > seen &&
               !peak.compare_exchange_weak(seen, local, std::memory_order_relaxed)) {
        }
    }
    std::atomic<std::size_t> frames{0};
    std::atomic<float> peak{0.0F};
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

// 子機は返送モニターを同じプロセスで再生する。返送を既定の再生デバイスで聞いていても、
// ループバックが返送音を集めて親機へ送り返し、相手に自分の声が戻ってはいけない（Issue #108）。
// テストのプロセスが鳴らす正弦波を返送の代わりにし、集まった音に含まれないことを確かめる。
TEST(WasapiLoopbackInputTest, ExcludesSoundPlayedByThisProcess) {
    test_support::TonePlayer tone;
    if (!tone.start()) {
        GTEST_SKIP() << "no float32 render device";
    }
    common::FakeLogger logger;
    auto input = make_wasapi_loopback_input(logger);
    ASSERT_TRUE(input.has_value());
    PeakCapture capture;
    CountingListener listener;
    const auto started = input.value()->start(capture, listener);
    if (!started && started.error() == AudioDeviceError::kDeviceNotFound) {
        GTEST_SKIP() << "no render device";
    }
    ASSERT_TRUE(started.has_value());
    std::this_thread::sleep_for(kRunTime);
    input.value()->stop();
    tone.stop();

    EXPECT_GT(capture.frames.load(), 0U);
    // エンドポイント全体のループバックなら、鳴らした振幅（0.5）がそのまま集まる。
    // ほかのアプリが鳴らしていなければ、集まるのは無音だけになる。
    constexpr float kLeakThreshold = test_support::TonePlayer::kAmplitude * 0.1F;
    EXPECT_LT(capture.peak.load(), kLeakThreshold);
}

}  // namespace
}  // namespace vocroz::capture
