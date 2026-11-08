// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機のセッション状態（SessionState）の有限状態機械（AGENTS.md 6.1）。
// 遷移は IDLE → CREATING → READY ⇄ ACTIVE → ENDING → IDLE。CREATING、READY、ACTIVE からは ERROR
// へ移り、ERROR からは IDLE へ戻る。遷移の規則をこの表 1 か所で決め、副作用は HostSession が行う。
#pragma once

#include <cstdint>
#include <optional>

#include "vocroz/control_protocol/states.h"

namespace vocroz::session::state {

/// セッション状態を進めるイベント。
enum class SessionEvent : std::uint8_t {
    kCreate,                ///< オペレーターがセッションの作成を指示した
    kCreated,               ///< 招待の登録が終わった
    kParticipantActive,     ///< CONNECTED か RECONNECTING の参加者が 1 人以上になった
    kNoParticipantActive,   ///< CONNECTED と RECONNECTING の参加者がいなくなった（全員 LOST など）
    kEnd,                   ///< オペレーターがセッションの終了を指示した
    kEnded,                 ///< 終了処理（招待の失効、切断、フェードアウト、保存）が終わった
    kFault,                 ///< 致命的なエラー（招待を登録できない、エンジンの異常）
    kRecover,               ///< オペレーターがエラーを確認した
};

/// 状態 current でイベント event が起きたときの次の状態を返す。
/// @return 次の状態。その状態で受け付けないイベントなら std::nullopt
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] std::optional<control_protocol::SessionState> next_state(
    control_protocol::SessionState current, SessionEvent event) noexcept;

}  // namespace vocroz::session::state
