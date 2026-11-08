// SPDX-License-Identifier: AGPL-3.0-only
//
// SignalMonitor の単体テスト。
// タイムアウトによる NO_SIGNAL の判定と、
// ミュートと解除を繰り返しても（通知とフレームの到着順が逆転しても）
// NO_SIGNAL を誤判定しないことを確かめる。

#include "vocroz/transport/signal_monitor.h"

#include <chrono>
#include <random>

#include <gtest/gtest.h>

#include "fakes/fake_clock.h"

namespace vocroz::transport {
namespace {

using namespace std::chrono_literals;

class SignalMonitorTest : public ::testing::Test {
protected:
    common::FakeClock clock_;
    SignalMonitor monitor_{clock_.now()};
};

TEST_F(SignalMonitorTest, StartsPaused) {
    EXPECT_EQ(monitor_.state(), SignalState::kPaused);
}

TEST_F(SignalMonitorTest, PausedNeverTimesOut) {
    clock_.advance(60s);
    monitor_.poll(clock_.now());
    EXPECT_EQ(monitor_.state(), SignalState::kPaused);
}

TEST_F(SignalMonitorTest, DropsFramesWhilePaused) {
    EXPECT_FALSE(monitor_.on_media_frame(0, clock_.now()));
    EXPECT_EQ(monitor_.state(), SignalState::kPaused);
}

TEST_F(SignalMonitorTest, ReceivesAfterUnmuteAndFirstFrame) {
    monitor_.on_send_mute(false, 1, clock_.now());
    EXPECT_EQ(monitor_.state(), SignalState::kResuming);
    clock_.advance(20ms);
    EXPECT_TRUE(monitor_.on_media_frame(1, clock_.now()));
    EXPECT_EQ(monitor_.state(), SignalState::kReceiving);
}

TEST_F(SignalMonitorTest, NoSignalAfter500msWithoutFrames) {
    monitor_.on_send_mute(false, 1, clock_.now());
    ASSERT_TRUE(monitor_.on_media_frame(1, clock_.now()));
    clock_.advance(499ms);
    monitor_.poll(clock_.now());
    EXPECT_EQ(monitor_.state(), SignalState::kReceiving);
    clock_.advance(1ms);
    monitor_.poll(clock_.now());
    EXPECT_EQ(monitor_.state(), SignalState::kNoSignal);
    // フレームが戻れば RECEIVING に戻る。
    EXPECT_TRUE(monitor_.on_media_frame(1, clock_.now()));
    EXPECT_EQ(monitor_.state(), SignalState::kReceiving);
}

TEST_F(SignalMonitorTest, ResumingWaitsOneSecondBeforeNoSignal) {
    monitor_.on_send_mute(false, 1, clock_.now());
    clock_.advance(999ms);
    monitor_.poll(clock_.now());
    EXPECT_EQ(monitor_.state(), SignalState::kResuming);
    clock_.advance(1ms);
    monitor_.poll(clock_.now());
    EXPECT_EQ(monitor_.state(), SignalState::kNoSignal);
}

TEST_F(SignalMonitorTest, FrameBeforeUnmuteNotificationIsAccepted) {
    // 解除の通知（制御）より先に、新しい世代のフレーム（メディア）が届いた場合。
    EXPECT_TRUE(monitor_.on_media_frame(1, clock_.now()));
    EXPECT_EQ(monitor_.state(), SignalState::kReceiving);
    monitor_.on_send_mute(false, 1, clock_.now());
    EXPECT_EQ(monitor_.state(), SignalState::kReceiving);
}

TEST_F(SignalMonitorTest, StaleFrameAfterMuteDoesNotLeavePaused) {
    monitor_.on_send_mute(false, 1, clock_.now());
    ASSERT_TRUE(monitor_.on_media_frame(1, clock_.now()));
    monitor_.on_send_mute(true, 1, clock_.now());
    EXPECT_EQ(monitor_.state(), SignalState::kPaused);
    // ミュート前に送られ、通知より後に届いたフレーム。
    EXPECT_FALSE(monitor_.on_media_frame(1, clock_.now()));
    EXPECT_EQ(monitor_.state(), SignalState::kPaused);
}

TEST_F(SignalMonitorTest, RepeatedMuteToggleWithReorderingNeverReportsNoSignal) {
    // 子機がミュートと解除を繰り返し、制御メッセージとフレームの到着が数 ms
    // ずつ前後する状況を作る。
    // 送信中のフレームは 5ms ごとに届き、途絶はないので、NO_SIGNAL になってはならない。
    std::mt19937 rng(7);  // 決定的にするため種を固定する
    std::uniform_int_distribution<int> length_ms(30, 800);
    std::uniform_int_distribution<int> lag_ms(0, 30);
    std::uint32_t epoch = 0;
    for (int toggle = 0; toggle < 200; ++toggle) {
        // 解除。
        // 通知とフレームのどちらが先に届くかを交互に変える。
        ++epoch;
        const int notify_lag = lag_ms(rng);
        const int run = length_ms(rng);
        bool notified = false;
        for (int t = 0; t < run; t += 5) {
            if (!notified && t >= notify_lag) {
                monitor_.on_send_mute(false, epoch, clock_.now());
                notified = true;
            }
            ASSERT_TRUE(monitor_.on_media_frame(epoch, clock_.now()));
            clock_.advance(5ms);
            monitor_.poll(clock_.now());
            ASSERT_NE(monitor_.state(), SignalState::kNoSignal) << "toggle " << toggle;
        }
        if (!notified) {
            monitor_.on_send_mute(false, epoch, clock_.now());
        }
        // ミュート。
        // 通知のあとにも、ミュート前のフレームが少し遅れて届く。
        monitor_.on_send_mute(true, epoch, clock_.now());
        for (int i = 0; i < 3; ++i) {
            EXPECT_FALSE(monitor_.on_media_frame(epoch, clock_.now()));
        }
        const int paused = length_ms(rng);
        for (int t = 0; t < paused; t += 50) {
            clock_.advance(50ms);
            monitor_.poll(clock_.now());
            ASSERT_EQ(monitor_.state(), SignalState::kPaused) << "toggle " << toggle;
        }
    }
}

}  // namespace
}  // namespace vocroz::transport
