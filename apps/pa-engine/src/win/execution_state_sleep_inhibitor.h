// SPDX-License-Identifier: AGPL-3.0-only
//
// SetThreadExecutionState によるシステムのスリープの抑止。
// セッションが ACTIVE の間だけ抑止し、画面のスリープは抑止しない。電源プランなど
// システムの設定は変えない（docs/adr/0008-runtime-behavior.md）。
// 抑止はそれを要求したスレッドに結び付くので、コントロールスレッドからだけ使う。
#pragma once

namespace vocroz::engine::win {

/// システムのスリープの抑止。破棄で必ず解除する。
/// スレッド安全性: 作ったスレッド（コントロールスレッド）からだけ使い、そのスレッドで破棄する。
class ExecutionStateSleepInhibitor {
public:
    ExecutionStateSleepInhibitor() = default;
    ExecutionStateSleepInhibitor(const ExecutionStateSleepInhibitor&) = delete;
    ExecutionStateSleepInhibitor& operator=(const ExecutionStateSleepInhibitor&) = delete;
    ~ExecutionStateSleepInhibitor();

    /// 抑止するかを切り替える。前回と同じ値なら何もしない。
    /// @param inhibit 抑止するなら true
    void set_inhibited(bool inhibit) noexcept;

private:
    bool inhibited_ = false;
};

}  // namespace vocroz::engine::win
