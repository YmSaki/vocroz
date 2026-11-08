// SPDX-License-Identifier: AGPL-3.0-only
//
// SendMuteTracker の単体テスト。
// 世代番号の比較で、到着順が逆転した通知と古いフレームを正しく扱うことを確かめる。

#include "vocroz/transport/send_mute_tracker.h"

#include <gtest/gtest.h>

namespace vocroz::transport {
namespace {

TEST(SendMuteTrackerTest, StartsMuted) {
    const SendMuteTracker tracker;
    EXPECT_TRUE(tracker.send_muted());
    EXPECT_EQ(tracker.epoch(), 0U);
}

TEST(SendMuteTrackerTest, DropsFramesWhileMuted) {
    SendMuteTracker tracker;
    EXPECT_EQ(tracker.classify(0), FrameVerdict::kDrop);
}

TEST(SendMuteTrackerTest, UnmuteThenMuteInSameEpoch) {
    SendMuteTracker tracker;
    EXPECT_EQ(tracker.apply(false, 1), MuteUpdate::kUnmuted);
    EXPECT_EQ(tracker.classify(1), FrameVerdict::kAccept);
    EXPECT_EQ(tracker.apply(true, 1), MuteUpdate::kMuted);
    // ミュートの通知より後に届いた、ミュート前のフレームは破棄する。
    EXPECT_EQ(tracker.classify(1), FrameVerdict::kDrop);
}

TEST(SendMuteTrackerTest, IgnoresUnmuteArrivingAfterMuteOfSameEpoch) {
    SendMuteTracker tracker;
    EXPECT_EQ(tracker.apply(true, 1), MuteUpdate::kMuted);
    EXPECT_EQ(tracker.apply(false, 1), MuteUpdate::kIgnored);
    EXPECT_TRUE(tracker.send_muted());
}

TEST(SendMuteTrackerTest, IgnoresNotificationsOfOlderEpoch) {
    SendMuteTracker tracker;
    ASSERT_EQ(tracker.apply(false, 3), MuteUpdate::kUnmuted);
    EXPECT_EQ(tracker.apply(true, 2), MuteUpdate::kIgnored);
    EXPECT_EQ(tracker.apply(false, 3), MuteUpdate::kIgnored);
    EXPECT_FALSE(tracker.send_muted());
}

TEST(SendMuteTrackerTest, FrameOfNewerEpochImpliesUnmute) {
    SendMuteTracker tracker;
    ASSERT_EQ(tracker.apply(true, 1), MuteUpdate::kMuted);
    // 解除の通知より先に、新しい世代のフレームが届いた。
    EXPECT_EQ(tracker.classify(2), FrameVerdict::kAcceptNewSegment);
    EXPECT_FALSE(tracker.send_muted());
    EXPECT_EQ(tracker.epoch(), 2U);
    // あとから届いた解除の通知は、すでに知っているので変化なし。
    EXPECT_EQ(tracker.apply(false, 2), MuteUpdate::kIgnored);
    EXPECT_EQ(tracker.classify(2), FrameVerdict::kAccept);
}

TEST(SendMuteTrackerTest, DropsFramesOfOlderEpoch) {
    SendMuteTracker tracker;
    ASSERT_EQ(tracker.apply(false, 2), MuteUpdate::kUnmuted);
    EXPECT_EQ(tracker.classify(1), FrameVerdict::kDrop);
}

}  // namespace
}  // namespace vocroz::transport
