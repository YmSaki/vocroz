// SPDX-License-Identifier: AGPL-3.0-only
//
// 1 ストリームの信号状態（RECEIVING / PAUSED / RESUMING / NO_SIGNAL）を判定する。
// フレームの到着、送信ミュートの通知、経過時間から状態を決め、ミュート前の古いフレームを再生側に渡さないよう選別する。
// 時刻は呼び出し側が Clock から取って渡す。テストで時刻を自由に進められるようにするため。
#pragma once

#include <cstdint>

#include "vocroz/common/ports/clock.h"
#include "vocroz/transport/send_mute_tracker.h"
#include "vocroz/transport/signal_state.h"

namespace vocroz::transport {

/// 信号状態の判定。初期状態は PAUSED（子機は送信ミュートで参加するため）。
/// スレッド安全性: 1 つのオブジェクトを同時に 1 スレッドからだけ使う。
/// 通常はネットワーク受信スレッドが、フレームと制御メッセージを受け取ったときと定期的な poll()
/// で呼ぶ。
class SignalMonitor {
public:
    using TimePoint = common::Clock::TimePoint;

    /// @param now 現在時刻
    explicit SignalMonitor(TimePoint now) noexcept;

    /// メディアフレームの到着を知らせる。
    /// @param mute_epoch フレームに載っていた送信ミュートの世代番号
    /// @param now        現在時刻
    /// @return 再生側（PlayoutStream）に渡すべきなら true。ミュート前やミュート中の世代のフレームは
    /// false
    [[nodiscard]] bool on_media_frame(std::uint32_t mute_epoch, TimePoint now) noexcept;

    /// 送信ミュートの通知を知らせる。古い通知は無視する。
    /// @param send_muted 通知されたミュート状態
    /// @param mute_epoch 通知に載っていた世代番号
    /// @param now        現在時刻
    void on_send_mute(bool send_muted, std::uint32_t mute_epoch, TimePoint now) noexcept;

    /// 経過時間によるタイムアウトを判定する。フレームの周期（数 ms）から 100ms 程度の間隔で呼ぶ。
    /// @param now 現在時刻
    void poll(TimePoint now) noexcept;

    /// 現在の信号状態を返す。
    [[nodiscard]] SignalState state() const noexcept {
        return state_;
    }

private:
    SignalState state_ = SignalState::kPaused;
    SendMuteTracker mute_;
    // 最後に現在の区間のフレームを受け付けた時刻。RECEIVING のタイムアウトの起点。
    TimePoint last_frame_;
    // 送信の再開（解除の通知）を知った時刻。RESUMING のタイムアウトの起点。
    TimePoint resume_started_;
};

}  // namespace vocroz::transport
