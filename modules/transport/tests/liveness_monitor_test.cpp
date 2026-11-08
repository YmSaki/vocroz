// SPDX-License-Identifier: AGPL-3.0-only
//
// LivenessMonitor の単体テスト。受信が 5 秒途絶したときに 1 回だけ知らせ、
// 受信が戻れば再開を知らせることを、FakeClock で確かめる。

#include "vocroz/transport/liveness_monitor.h"

#include <chrono>

#include <gtest/gtest.h>

#include "vocroz/transport/ports/liveness_listener.h"

#include "fakes/fake_clock.h"

namespace vocroz::transport {
namespace {

using std::chrono::milliseconds;
using std::chrono::seconds;

class CountingListener final : public LivenessListener {
public:
    void on_peer_timed_out() override {
        ++timed_out;
    }
    void on_peer_resumed() override {
        ++resumed;
    }

    int timed_out = 0;
    int resumed = 0;
};

class LivenessMonitorTest : public ::testing::Test {
protected:
    void advance_and_poll(common::Clock::TimePoint::duration delta) {
        clock_.advance(delta);
        monitor_.poll();
    }

    common::FakeClock clock_;
    CountingListener listener_;
    LivenessMonitor monitor_{clock_, listener_};
};

TEST_F(LivenessMonitorTest, TimesOutFiveSecondsAfterCreationWithoutPackets) {
    EXPECT_EQ(monitor_.next_deadline(), clock_.now() + seconds{5});
    advance_and_poll(milliseconds{4999});
    EXPECT_EQ(listener_.timed_out, 0);
    advance_and_poll(milliseconds{1});
    EXPECT_EQ(listener_.timed_out, 1);
    EXPECT_TRUE(monitor_.timed_out());
}

TEST_F(LivenessMonitorTest, EachReceivedPacketRestartsTheTimeout) {
    advance_and_poll(seconds{4});
    monitor_.note_packet_received();
    EXPECT_EQ(monitor_.next_deadline(), clock_.now() + seconds{5});
    advance_and_poll(milliseconds{4999});
    EXPECT_EQ(listener_.timed_out, 0);
    advance_and_poll(milliseconds{1});
    EXPECT_EQ(listener_.timed_out, 1);
}

TEST_F(LivenessMonitorTest, NotifiesTimeoutOnlyOnceWhileSilent) {
    advance_and_poll(seconds{5});
    advance_and_poll(seconds{5});
    advance_and_poll(seconds{60});
    EXPECT_EQ(listener_.timed_out, 1);
    EXPECT_EQ(listener_.resumed, 0);
}

TEST_F(LivenessMonitorTest, NotifiesResumeWhenPacketsReturnAndCanTimeOutAgain) {
    advance_and_poll(seconds{5});
    clock_.advance(seconds{3});
    monitor_.note_packet_received();
    monitor_.poll();
    EXPECT_EQ(listener_.resumed, 1);
    EXPECT_FALSE(monitor_.timed_out());

    advance_and_poll(seconds{5});
    EXPECT_EQ(listener_.timed_out, 2);
}

TEST_F(LivenessMonitorTest, KeepsPollingWhileTimedOutToNoticeResume) {
    advance_and_poll(seconds{5});
    EXPECT_EQ(monitor_.next_deadline(), clock_.now() + seconds{1});
}

}  // namespace
}  // namespace vocroz::transport
