// SPDX-License-Identifier: AGPL-3.0-only
//
// 信号状態の遷移表。
// 状態 × イベント → 次状態を constexpr の表で 1 か所に定義する（AGENTS.md 6.1）。
// 時間の判定やミュートの世代番号の比較は SignalMonitor が行い、ここには純粋な遷移規則だけを置く。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "vocroz/transport/signal_state.h"

namespace vocroz::transport {

/// 信号状態を変えるイベント。
enum class SignalEvent : std::uint8_t {
    kFrameAccepted,  ///< 現在の区間の有効なフレームが届いた
    kSendMuted,      ///< 送信ミュートの通知（send_muted = true）を受け取った
    kSendUnmuted,    ///< 送信ミュートの解除通知（send_muted =
                   ///< false）を受け取ったか、新しい区間のフレームで解除を知った
    kReceiveTimeout,  ///< RECEIVING のまま 500ms フレームが届かなかった
    kResumeTimeout,   ///< RESUMING のまま 1 秒フレームが届かなかった
};

namespace detail {

inline constexpr std::size_t kSignalStateCount = 4;
inline constexpr std::size_t kSignalEventCount = 5;

using S = SignalState;

// 行が現在の状態、列がイベント（SignalEvent の宣言順）。
// - 送信ミュートの通知は、どの状態からでも PAUSED にする。
// ミュート中のフレーム不着を NO_SIGNAL にしないため。
// - 解除の通知は RESUMING にし、最初のフレームを待つ。
// - タイムアウトは、それを監視している状態（RECEIVING の 500ms、RESUMING の 1 秒）
// でだけ NO_SIGNAL にする。
//   PAUSED 中のタイムアウトは無視する（死活はキープアライブで監視する）。
inline constexpr std::array<std::array<SignalState, kSignalEventCount>, kSignalStateCount>
    kSignalTransitions{{
        // kFrameAccepted kSendMuted  kSendUnmuted  kReceiveTimeout kResumeTimeout
        {S::kReceiving, S::kPaused, S::kResuming, S::kNoSignal, S::kReceiving},  // kReceiving
        {S::kReceiving, S::kPaused, S::kResuming, S::kPaused, S::kPaused},       // kPaused
        {S::kReceiving, S::kPaused, S::kResuming, S::kResuming, S::kNoSignal},   // kResuming
        {S::kReceiving, S::kPaused, S::kResuming, S::kNoSignal, S::kNoSignal},   // kNoSignal
    }};

}  // namespace detail

/// 次の信号状態を返す。
/// 副作用はない。
/// @param state 現在の状態
/// @param event 起きたイベント
/// @return 次の状態
/// スレッド安全性: どのスレッドからも呼び出し可能。
[[nodiscard]] constexpr SignalState next_signal_state(SignalState state,
                                                      SignalEvent event) noexcept {
    const auto row = static_cast<std::size_t>(state);
    const auto column = static_cast<std::size_t>(event);
    if (row >= detail::kSignalStateCount || column >= detail::kSignalEventCount) {
        return SignalState::kNoSignal;  // 未定義の値は無音側に倒す
    }
    return detail::kSignalTransitions[row][column];
}

}  // namespace vocroz::transport
