// SPDX-License-Identifier: AGPL-3.0-only
//
// JitterBuffer の単体テスト。欠損、順序逆転、重複、遅着を与えても、
// タイムスタンプの順にだけ取り出し、再生位置より古いフレームを取り出さないことを確かめる。

#include "jitter_buffer.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::transport {
namespace {

constexpr std::size_t kFrame = 240;
constexpr std::size_t kCapacity = 16;

MediaFrame frame_at(std::uint64_t index) {
    MediaFrame frame;
    frame.timestamp = index * kFrame;
    frame.mute_epoch = 1;
    frame.payload_size = 1;
    frame.payload[0] = static_cast<std::uint8_t>(index);
    return frame;
}

TEST(JitterBufferTest, PopsFramesInTimestampOrderDespiteReordering) {
    JitterBuffer buffer(kFrame, kCapacity);
    for (const std::uint64_t i : {2U, 0U, 3U, 1U}) {
        ASSERT_EQ(buffer.insert(frame_at(i)), InsertResult::kAccepted);
    }
    EXPECT_EQ(buffer.depth_samples(), 4 * kFrame);
    buffer.start();
    for (std::uint64_t i = 0; i < 4; ++i) {
        const PopResult popped = buffer.pop();
        ASSERT_EQ(popped.kind, PopKind::kFrame);
        EXPECT_EQ(popped.frame->timestamp, i * kFrame);
    }
    EXPECT_EQ(buffer.pop().kind, PopKind::kEmpty);
}

TEST(JitterBufferTest, RejectsDuplicates) {
    JitterBuffer buffer(kFrame, kCapacity);
    ASSERT_EQ(buffer.insert(frame_at(0)), InsertResult::kAccepted);
    ASSERT_EQ(buffer.insert(frame_at(1)), InsertResult::kAccepted);
    EXPECT_EQ(buffer.insert(frame_at(1)), InsertResult::kDuplicate);
    EXPECT_EQ(buffer.stored_frames(), 2U);
}

TEST(JitterBufferTest, ReportsMissingFrameWhenLaterFramesExist) {
    JitterBuffer buffer(kFrame, kCapacity);
    ASSERT_EQ(buffer.insert(frame_at(0)), InsertResult::kAccepted);
    ASSERT_EQ(buffer.insert(frame_at(2)), InsertResult::kAccepted);
    buffer.start();
    EXPECT_EQ(buffer.pop().kind, PopKind::kFrame);
    EXPECT_EQ(buffer.pop().kind, PopKind::kMissing);
    EXPECT_EQ(buffer.pop().kind, PopKind::kFrame);
}

TEST(JitterBufferTest, RejectsFrameOlderThanPlayoutPosition) {
    JitterBuffer buffer(kFrame, kCapacity);
    ASSERT_EQ(buffer.insert(frame_at(0)), InsertResult::kAccepted);
    ASSERT_EQ(buffer.insert(frame_at(2)), InsertResult::kAccepted);
    buffer.start();
    static_cast<void>(buffer.pop());
    static_cast<void>(buffer.pop());  // 1 は欠損として PLC で埋めた扱い
    // 欠損として埋めたあとに届いた 1 は、古い音なので受け付けない。
    EXPECT_EQ(buffer.insert(frame_at(1)), InsertResult::kLate);
    EXPECT_EQ(buffer.insert(frame_at(0)), InsertResult::kLate);
}

TEST(JitterBufferTest, PlayoutPositionAdvancesDuringUnderrun) {
    JitterBuffer buffer(kFrame, kCapacity);
    ASSERT_EQ(buffer.insert(frame_at(0)), InsertResult::kAccepted);
    buffer.start();
    static_cast<void>(buffer.pop());
    EXPECT_EQ(buffer.pop().kind, PopKind::kEmpty);
    // 枯渇の間も再生位置は実時間と同じく進むので、枯渇中の位置のフレームは遅着になる。
    EXPECT_EQ(buffer.insert(frame_at(1)), InsertResult::kLate);
    EXPECT_EQ(buffer.insert(frame_at(2)), InsertResult::kAccepted);
}

TEST(JitterBufferTest, RejectsMisalignedTimestamp) {
    JitterBuffer buffer(kFrame, kCapacity);
    ASSERT_EQ(buffer.insert(frame_at(0)), InsertResult::kAccepted);
    MediaFrame frame = frame_at(1);
    frame.timestamp += 7;
    EXPECT_EQ(buffer.insert(frame), InsertResult::kMisaligned);
}

TEST(JitterBufferTest, ResynchronizesOnFrameBeyondCapacity) {
    JitterBuffer buffer(kFrame, kCapacity);
    ASSERT_EQ(buffer.insert(frame_at(0)), InsertResult::kAccepted);
    ASSERT_EQ(buffer.insert(frame_at(1)), InsertResult::kAccepted);
    buffer.start();
    EXPECT_EQ(buffer.insert(frame_at(1000)), InsertResult::kDiscontinuity);
    EXPECT_FALSE(buffer.started());
    EXPECT_EQ(buffer.stored_frames(), 1U);
    buffer.start();
    const PopResult popped = buffer.pop();
    ASSERT_EQ(popped.kind, PopKind::kFrame);
    EXPECT_EQ(popped.frame->timestamp, 1000 * kFrame);
}

TEST(JitterBufferTest, KeepsNewestFramesWhenTooManyArriveBeforeStart) {
    JitterBuffer buffer(kFrame, kCapacity);
    for (std::uint64_t i = 0; i < kCapacity + 4; ++i) {
        ASSERT_EQ(buffer.insert(frame_at(i)), InsertResult::kAccepted);
    }
    EXPECT_EQ(buffer.stored_frames(), kCapacity);
    buffer.start();
    const PopResult popped = buffer.pop();
    ASSERT_EQ(popped.kind, PopKind::kFrame);
    EXPECT_EQ(popped.frame->timestamp, 4 * kFrame);
}

TEST(JitterBufferTest, DropUntilDiscardsOldestFrames) {
    JitterBuffer buffer(kFrame, kCapacity);
    for (std::uint64_t i = 0; i < 10; ++i) {
        ASSERT_EQ(buffer.insert(frame_at(i)), InsertResult::kAccepted);
    }
    buffer.start();
    EXPECT_EQ(buffer.drop_until(3 * kFrame), 7U);
    EXPECT_EQ(buffer.depth_samples(), 3 * kFrame);
    const PopResult popped = buffer.pop();
    ASSERT_EQ(popped.kind, PopKind::kFrame);
    EXPECT_EQ(popped.frame->timestamp, 7 * kFrame);
}

TEST(JitterBufferTest, RebufferKeepsNewerFramesAndRejectsOlderOnes) {
    JitterBuffer buffer(kFrame, kCapacity);
    ASSERT_EQ(buffer.insert(frame_at(0)), InsertResult::kAccepted);
    ASSERT_EQ(buffer.insert(frame_at(5)), InsertResult::kAccepted);
    buffer.start();
    for (int i = 0; i < 3; ++i) {
        static_cast<void>(buffer.pop());
    }
    buffer.rebuffer();
    EXPECT_FALSE(buffer.started());
    EXPECT_EQ(buffer.stored_frames(), 1U);
    // 溜め直しの間も、すでに再生（または PLC で補間）した位置より古いフレームは受け付けない。
    EXPECT_EQ(buffer.insert(frame_at(2)), InsertResult::kLate);
    EXPECT_EQ(buffer.insert(frame_at(4)), InsertResult::kAccepted);
    buffer.start();
    const PopResult popped = buffer.pop();
    ASSERT_EQ(popped.kind, PopKind::kFrame);
    EXPECT_EQ(popped.frame->timestamp, 4 * kFrame);
}

TEST(JitterBufferTest, FloorAdvancesWhileNothingArrivesAfterRebuffer) {
    JitterBuffer buffer(kFrame, kCapacity);
    ASSERT_EQ(buffer.insert(frame_at(0)), InsertResult::kAccepted);
    buffer.start();
    static_cast<void>(buffer.pop());
    buffer.rebuffer();
    buffer.advance_floor(10 * kFrame);
    // 無音で埋めていた 10 フレームぶんの位置のフレームは古いので受け付けない。
    EXPECT_EQ(buffer.insert(frame_at(5)), InsertResult::kLate);
    EXPECT_EQ(buffer.insert(frame_at(11)), InsertResult::kAccepted);
}

TEST(JitterBufferTest, ResetClearsEverything) {
    JitterBuffer buffer(kFrame, kCapacity);
    ASSERT_EQ(buffer.insert(frame_at(3)), InsertResult::kAccepted);
    buffer.start();
    buffer.reset();
    EXPECT_FALSE(buffer.started());
    EXPECT_EQ(buffer.stored_frames(), 0U);
    EXPECT_EQ(buffer.depth_samples(), 0U);
    // 新しい区間では、前の区間より古いタイムスタンプでも受け付ける。
    EXPECT_EQ(buffer.insert(frame_at(1)), InsertResult::kAccepted);
}

TEST(JitterBufferTest, NeverPopsOlderOrRepeatedFramesUnderRandomImpairment) {
    // 欠損、順序逆転、重複、ジッターを乱数で与え、取り出したフレームのタイムスタンプが
    // 狭義単調増加であること（古い音も同じ音も再生しない）を確かめる。
    std::mt19937 rng(12345);  // 決定的にするため種を固定する
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    JitterBuffer buffer(kFrame, kCapacity);

    std::vector<std::uint64_t> pending;
    std::uint64_t next = 0;
    bool have_last = false;
    std::uint64_t last = 0;
    std::size_t popped_frames = 0;
    for (int step = 0; step < 20000; ++step) {
        // 送信側: 1 周期に 1 フレーム。10% を欠損させ、5% を重複させる。
        if (unit(rng) >= 0.1) {
            pending.push_back(next);
            if (unit(rng) < 0.05) {
                pending.push_back(next);
            }
        }
        ++next;
        // 経路: 届く順をかき混ぜ、ときどきまとめて届ける（ジッター）。
        std::shuffle(pending.begin(), pending.end(), rng);
        const auto deliver = static_cast<std::size_t>(unit(rng) * 3.0);
        for (std::size_t i = 0; i < deliver && !pending.empty(); ++i) {
            static_cast<void>(buffer.insert(frame_at(pending.back())));
            pending.pop_back();
        }
        if (!buffer.started()) {
            if (buffer.depth_samples() >= 3 * kFrame) {
                buffer.start();
            }
            continue;
        }
        const PopResult popped = buffer.pop();
        if (popped.kind == PopKind::kFrame) {
            if (have_last) {
                ASSERT_GT(popped.frame->timestamp, last);
            }
            last = popped.frame->timestamp;
            have_last = true;
            ++popped_frames;
        }
    }
    EXPECT_GT(popped_frames, 10000U);
}

}  // namespace
}  // namespace vocroz::transport
