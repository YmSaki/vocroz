// SPDX-License-Identifier: AGPL-3.0-only
//
// DiskIoWorker の単体テスト。登録した仕事が周期ごとに回ること、外した仕事は回らないこと、
// 止めるときに最後の 1 周期を回して後始末を済ませることを確かめる。

#include "disk_io_worker.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

namespace vocroz::engine {
namespace {

using namespace std::chrono_literals;

class CountingJob final : public DiskJob {
public:
    void service() override {
        count_.fetch_add(1, std::memory_order_relaxed);
    }
    [[nodiscard]] int count() const noexcept {
        return count_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<int> count_{0};
};

TEST(DiskIoWorkerTest, RunOnceServicesEveryAddedJob) {
    DiskIoWorker worker(10ms);
    auto a = std::make_shared<CountingJob>();
    auto b = std::make_shared<CountingJob>();
    worker.add(a);
    worker.add(b);
    worker.run_once();
    EXPECT_EQ(a->count(), 1);
    EXPECT_EQ(b->count(), 1);
}

TEST(DiskIoWorkerTest, RemovedJobIsNoLongerServiced) {
    DiskIoWorker worker(10ms);
    auto job = std::make_shared<CountingJob>();
    worker.add(job);
    worker.remove(job);
    worker.run_once();
    EXPECT_EQ(job->count(), 0);
}

TEST(DiskIoWorkerTest, ThreadServicesJobsPeriodically) {
    DiskIoWorker worker(1ms);
    auto job = std::make_shared<CountingJob>();
    worker.add(job);
    worker.start();
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (job->count() < 3 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    worker.stop();
    EXPECT_GE(job->count(), 3);
}

TEST(DiskIoWorkerTest, StopRunsOneLastCycle) {
    // 止める直前に閉じるよう頼まれたファイルも、スレッドを止める前の最後の周期で閉じる。
    DiskIoWorker worker(1h);
    auto job = std::make_shared<CountingJob>();
    worker.start();
    worker.add(job);
    worker.stop();
    EXPECT_GE(job->count(), 1);
}

TEST(DiskIoWorkerTest, DestructorStopsWithoutStart) {
    auto job = std::make_shared<CountingJob>();
    {
        DiskIoWorker worker(10ms);
        worker.add(job);
    }
    SUCCEED();
}

}  // namespace
}  // namespace vocroz::engine
