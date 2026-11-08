// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機から見た参加者の接続状態（ConnectionState）の有限状態機械（AGENTS.md 6.1）。
// PENDING の参加者は承認でストリップを得て CONNECTING になり、以後は participant_id に固定される。
// 再接続トークンを持つ子機は、RECONNECTING、LOST、FAILED のどこからでも同じストリップへ戻れる
// （CONNECTING を経て CONNECTED）。
#pragma once

#include <cstdint>
#include <optional>

#include "vocroz/control_protocol/states.h"

namespace vocroz::session::state {

/// 接続状態を進めるイベント。
enum class ConnectionEvent : std::uint8_t {
    kApprove,           ///< オペレーターが参加を承認した
    kEstablished,       ///< 経路と鍵交換が終わり、メディアを受け付け始めた
    kPathFailed,        ///< 経路を確立できなかった（NAT 越えの不成立など）
    kReceiveTimeout,    ///< 音声と制御のどちらも 5 秒届かなかった
    kResumed,           ///< 途絶えていた同じ接続から、またパケットが届いた
    kGraceExpired,      ///< RECONNECTING のまま再接続の猶予（120 秒）が過ぎた
    kReconnectStarted,  ///< 再接続トークンを持つ子機が、新しい接続で戻ってきた
    kPeerLeft,          ///< 子機が退出したか、接続が閉じられた
};

/// 状態 current でイベント event が起きたときの次の状態を返す。
/// @return 次の状態。その状態で受け付けないイベントなら std::nullopt
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] std::optional<control_protocol::ConnectionState> next_state(
    control_protocol::ConnectionState current, ConnectionEvent event) noexcept;

}  // namespace vocroz::session::state
