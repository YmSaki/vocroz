// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機の接続状態（ClientState）の有限状態機械（AGENTS.md 6.1）。
// 招待の解決 → 参加の申請 → 承認待ち → 経路の確立 → 接続済みと進み、どの段階でも退出できる。
// FAILED は終端的な失敗で、自動では再試行しない（出演者が参加し直す）。
#pragma once

#include <cstdint>
#include <optional>

#include "vocroz/control_protocol/states.h"

namespace vocroz::session::state {

/// 子機の接続状態を進めるイベント。
enum class ClientEvent : std::uint8_t {
    kJoin,              ///< 出演者が招待で参加を指示した
    kResolved,          ///< 招待コードを解決し、親機への接続と申請を始めた
    kJoinPending,       ///< 親機が参加の申請を受け付けた（承認待ち）
    kApproved,          ///< 親機が承認した（再接続トークンによる復帰を含む）
    kMediaEstablished,  ///< 承認後の経路の確立が終わり、送信を始められる
    kLinkLost,          ///< 承認後に親機からの受信が 5 秒途絶えた
    kReconnected,       ///< 途絶の後、同じストリップへ戻れた
    kFailed,            ///< 終端的に失敗した（原因は呼び出し側が持つ）
    kLeave,             ///< 出演者が退出した
    kSessionEnded,      ///< 親機がセッションを終了した
};

/// 状態 current でイベント event が起きたときの次の状態を返す。
/// @return 次の状態。その状態で受け付けないイベントなら std::nullopt
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] std::optional<control_protocol::ClientState> next_state(
    control_protocol::ClientState current, ClientEvent event) noexcept;

}  // namespace vocroz::session::state
