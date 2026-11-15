// SPDX-License-Identifier: AGPL-3.0-only
//
// disk_io_worker.h の実装。

#include "disk_io_worker.h"

#include <algorithm>
#include <utility>

namespace vocroz::engine {

DiskIoWorker::DiskIoWorker(std::chrono::milliseconds interval) : interval_(interval) {}

DiskIoWorker::~DiskIoWorker() {
    stop();
}

void DiskIoWorker::add(std::shared_ptr<DiskJob> job) {
    if (!job) {
        return;
    }
    const std::lock_guard lock(mutex_);
    jobs_.push_back(std::move(job));
}

void DiskIoWorker::remove(const std::shared_ptr<DiskJob>& job) {
    const std::lock_guard lock(mutex_);
    std::erase(jobs_, job);
}

void DiskIoWorker::start() {
    const std::lock_guard lock(mutex_);
    if (thread_.joinable()) {
        return;
    }
    stopping_ = false;
    thread_ = std::thread([this] { loop(); });
}

void DiskIoWorker::stop() {
    {
        const std::lock_guard lock(mutex_);
        if (!thread_.joinable()) {
            return;
        }
        stopping_ = true;
    }
    wake_.notify_all();
    thread_.join();
}

void DiskIoWorker::run_once() {
    std::vector<std::shared_ptr<DiskJob>> jobs;
    {
        // 一覧は写してからロックの外で回す。ファイルの読み書きで止まっている間も、コントロール
        // スレッドの add() と remove() を待たせないため。
        const std::lock_guard lock(mutex_);
        jobs = jobs_;
    }
    for (const auto& job : jobs) {
        job->service();
    }
}

void DiskIoWorker::loop() {
    for (;;) {
        run_once();
        std::unique_lock lock(mutex_);
        if (wake_.wait_for(lock, interval_, [this] { return stopping_; })) {
            lock.unlock();
            // 止める前の最後の周期。録音タップを外した後の残りを書き出す。
            run_once();
            return;
        }
    }
}

}  // namespace vocroz::engine
