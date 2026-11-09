// SPDX-License-Identifier: AGPL-3.0-only
//
// セッションの状態から、プロセスとして OS に示す振る舞い（スリープの抑止、更新の抑止、
// 自動終了）を決める規則。判断だけを純粋関数とクラスに置き、OS への反映（SetThreadExecutionState、
// 名前付きミューテックス）はコンポジションルートが win/ のアダプターで行う。
#pragma once

#include <chrono>
#include <optional>

#include "vocroz/common/ports/clock.h"
#include "vocroz/control_protocol/states.h"

namespace vocroz::engine {

/// セッションの状態から決まる、プロセスの振る舞い。
struct EngineActivity {
    /// セッションがある（IDLE 以外）。pa-updater に「本番中」を知らせ、更新を止める。
    /// ERROR も含める。原因がわからないまま更新でファイルを入れ替えないため（フェイルセーフ）。
    bool session_exists = false;
    /// システムのスリープを抑止する。ACTIVE のときだけ（docs/adr/0008-runtime-behavior.md）。
    bool prevent_sleep = false;

    friend bool operator==(const EngineActivity&, const EngineActivity&) = default;
};

/// セッションの状態からプロセスの振る舞いを決める。
/// @param state セッションの状態
/// @return 振る舞い
/// スレッド安全性: 純粋関数。どのスレッドからも呼び出し可能。
[[nodiscard]] EngineActivity activity_for(control_protocol::SessionState state) noexcept;

/// 「セッションも UI もない」状態が続いたかを数える。
/// remote-pa を閉じたあとにエンジンだけが残り続けないよう、この状態が timeout 続いたら終了する。
/// UI だけがない（UI が落ちた）間は、セッションがあれば終了しない（UI の障害で音を止めない）。
/// スレッド安全性: 1 つのスレッド（コントロールスレッド）から使う。
class IdleShutdownTimer {
public:
    /// @param timeout 終了するまでの時間
    explicit IdleShutdownTimer(std::chrono::steady_clock::duration timeout) noexcept
        : timeout_(timeout) {}

    /// 今の状態を記録し、終了すべきかを返す。
    /// @param now            現在時刻
    /// @param ui_connected   UI がつながっている
    /// @param session_exists セッションがある
    /// @return セッションも UI もない状態が timeout 以上続いていれば true
    [[nodiscard]] bool should_exit(common::Clock::TimePoint now, bool ui_connected,
                                   bool session_exists) noexcept;

private:
    std::chrono::steady_clock::duration timeout_;
    // セッションも UI もない状態になった時刻。その状態でなければ std::nullopt。
    std::optional<common::Clock::TimePoint> idle_since_;
};

}  // namespace vocroz::engine
