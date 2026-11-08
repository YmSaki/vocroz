// SPDX-License-Identifier: AGPL-3.0-only
//
// vocroz::common::SpscQueue の単体テスト。
// 単一スレッドでの境界条件と、生産者 1・消費者 1 の並行動作を検証する。

#include "vocroz/common/spsc_queue.h"

#include <cstdint>
#include <thread>

#include <gtest/gtest.h>

namespace vocroz::common {
namespace {

TEST(SpscQueueTest, PopsInPushOrder) {
    SpscQueue<int, 4> queue;
    ASSERT_TRUE(queue.try_push(1));
    ASSERT_TRUE(queue.try_push(2));
    EXPECT_EQ(queue.try_pop(), 1);
    EXPECT_EQ(queue.try_pop(), 2);
}

TEST(SpscQueueTest, ReturnsNulloptWhenEmpty) {
    SpscQueue<int, 4> queue;
    EXPECT_FALSE(queue.try_pop().has_value());
}

TEST(SpscQueueTest, RejectsPushWhenFullAndKeepsContents) {
    SpscQueue<int, 2> queue;
    ASSERT_TRUE(queue.try_push(1));
    ASSERT_TRUE(queue.try_push(2));
    EXPECT_FALSE(queue.try_push(3));
    EXPECT_EQ(queue.try_pop(), 1);
    EXPECT_EQ(queue.try_pop(), 2);
    EXPECT_FALSE(queue.try_pop().has_value());
}

TEST(SpscQueueTest, ReusesSlotsAfterWrapAround) {
    // 容量を何周も超えて出し入れしても、順序と内容が保たれる。
    SpscQueue<int, 4> queue;
    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(queue.try_push(i));
        ASSERT_TRUE(queue.try_push(i + 1000));
        EXPECT_EQ(queue.try_pop(), i);
        EXPECT_EQ(queue.try_pop(), i + 1000);
    }
    EXPECT_EQ((SpscQueue<int, 4>::capacity()), 4U);
}

TEST(SpscQueueTest, DeliversAllItemsInOrderAcrossThreads) {
    // 生産者 1・消費者 1 で、取りこぼし、重複、順序の入れ替わりがないことを確かめる。
    // 容量を小さくして、満杯と空の両方の分岐を並行に通るようにしている。
    constexpr std::uint32_t kItemCount = 1'000'000;
    SpscQueue<std::uint32_t, 64> queue;

    std::thread producer([&queue] {
        for (std::uint32_t i = 0; i < kItemCount;) {
            if (queue.try_push(i)) {
                ++i;
            } else {
                std::this_thread::yield();
            }
        }
    });

    std::uint32_t expected = 0;
    bool in_order = true;
    while (expected < kItemCount) {
        if (const auto item = queue.try_pop()) {
            in_order = in_order && (*item == expected);
            ++expected;
        } else {
            std::this_thread::yield();
        }
    }
    producer.join();

    EXPECT_TRUE(in_order);
    EXPECT_FALSE(queue.try_pop().has_value());
}

}  // namespace
}  // namespace vocroz::common
