// SPDX-License-Identifier: AGPL-3.0-only
//
// ControlLoop の単体テスト。実際のスレッドで、積んだ処理がループのスレッドで順に実行されることと、
// 停止の要求で run() が戻ることを確かめる。

#include "control_loop.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace vocroz::engine {
namespace {

using std::chrono_literals::operator""ms;

TEST(ControlLoopTest, RunsPostedTasksInOrderOnTheLoopThread) {
    ControlLoop loop;
    std::vector<int> order;
    std::thread::id task_thread;
    loop.post([&] { order.push_back(1); });
    loop.post([&] {
        order.push_back(2);
        task_thread = std::this_thread::get_id();
        loop.request_stop();
    });
    loop.run(1000ms, [] {});
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
    EXPECT_EQ(task_thread, std::this_thread::get_id());
}

TEST(ControlLoopTest, CallsTickPeriodically) {
    ControlLoop loop;
    int ticks = 0;
    loop.run(1ms, [&] {
        if (++ticks == 3) {
            loop.request_stop();
        }
    });
    EXPECT_EQ(ticks, 3);
}

TEST(ControlLoopTest, StopFromAnotherThreadEndsRun) {
    ControlLoop loop;
    std::thread stopper([&] {
        std::this_thread::sleep_for(10ms);
        loop.request_stop();
    });
    loop.run(1000ms, [] {});
    stopper.join();
    SUCCEED();
}

// 停止のあとに積んだ処理は実行しない（破棄の途中の session を呼ばないため）。
TEST(ControlLoopTest, IgnoresTasksPostedAfterStop) {
    ControlLoop loop;
    bool ran = false;
    loop.request_stop();
    loop.post([&] { ran = true; });
    loop.run(1ms, [] {});
    EXPECT_FALSE(ran);
}

// 処理の中から積んだ処理も、同じ run() の中で実行する。
TEST(ControlLoopTest, AcceptsTasksPostedFromATask) {
    ControlLoop loop;
    bool nested = false;
    loop.post([&] {
        loop.post([&] {
            nested = true;
            loop.request_stop();
        });
    });
    loop.run(1000ms, [] {});
    EXPECT_TRUE(nested);
}

}  // namespace
}  // namespace vocroz::engine
