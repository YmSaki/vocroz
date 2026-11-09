// SPDX-License-Identifier: AGPL-3.0-only
//
// control_loop.h の実装。

#include "control_loop.h"

#include <utility>

namespace vocroz::engine {

void ControlLoop::post(Task task) {
    {
        const std::lock_guard lock{mutex_};
        if (stop_requested_) {
            return;
        }
        tasks_.push_back(std::move(task));
    }
    wake_.notify_one();
}

void ControlLoop::request_stop() noexcept {
    {
        const std::lock_guard lock{mutex_};
        stop_requested_ = true;
    }
    wake_.notify_one();
}

void ControlLoop::run(std::chrono::milliseconds interval, const std::function<void()>& on_tick) {
    // 周期は steady_clock で数える。処理の実行時間で周期が伸びていかないよう、
    // 前回の予定時刻に interval を足して次の予定時刻を決める。
    auto next_tick = std::chrono::steady_clock::now() + interval;
    std::unique_lock lock{mutex_};
    while (!stop_requested_) {
        wake_.wait_until(lock, next_tick, [this] { return stop_requested_ || !tasks_.empty(); });
        if (stop_requested_) {
            break;
        }
        // 処理はロックを外して実行する。処理の中から post() してもデッドロックしないように。
        while (!tasks_.empty() && !stop_requested_) {
            Task task = std::move(tasks_.front());
            tasks_.pop_front();
            lock.unlock();
            task();
            lock.lock();
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_tick && !stop_requested_) {
            lock.unlock();
            on_tick();
            lock.lock();
            next_tick += interval;
            // スリープからの復帰などで大きく遅れたら、遅れたぶんをまとめて呼ばずに今から数え直す。
            if (next_tick <= now) {
                next_tick = now + interval;
            }
        }
    }
    tasks_.clear();
}

}  // namespace vocroz::engine
