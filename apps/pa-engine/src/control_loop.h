// SPDX-License-Identifier: AGPL-3.0-only
//
// コントロールスレッドのイベントループ。
// session の HostSession はコントロールスレッドからだけ呼ぶ約束なので、UI の受信スレッド、
// デバイスのスレッド、ネットワーク受信スレッドからの通知は、ここへ処理として積んで
// コントロールスレッドで実行する（AGENTS.md 2.3、docs/adr/0028-engine-assembly.md）。
// オーディオスレッドからは使わない（キューの保護に mutex を使うため）。
#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

namespace vocroz::engine {

/// コントロールスレッドのイベントループ。
/// スレッド安全性: post() と request_stop() はオーディオスレッド以外のどのスレッドからも
/// 呼び出し可能。run() はコントロールスレッドから 1 回だけ呼ぶ。
class ControlLoop {
public:
    /// コントロールスレッドで実行する処理。
    using Task = std::function<void()>;

    ControlLoop() = default;
    ControlLoop(const ControlLoop&) = delete;
    ControlLoop& operator=(const ControlLoop&) = delete;
    ~ControlLoop() = default;

    /// 処理を積む。積んだ順に、次の周期を待たずにコントロールスレッドで実行する。
    /// 停止の要求のあとに積んだ処理は実行しない。
    /// @param task 実行する処理
    void post(Task task);

    /// run() を終わらせる。実行中の処理と周期処理が終わってから戻る。
    void request_stop() noexcept;

    /// 停止を要求されるまで、積まれた処理と周期処理を実行する。
    /// 周期処理は interval ごとに呼ぶ。処理が積まれても周期はずらさない。
    /// @param interval 周期処理の間隔
    /// @param on_tick  周期処理
    void run(std::chrono::milliseconds interval, const std::function<void()>& on_tick);

private:
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Task> tasks_;
    bool stop_requested_ = false;
};

}  // namespace vocroz::engine
