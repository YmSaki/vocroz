// SPDX-License-Identifier: AGPL-3.0-only
//
// 参加者の接続状態の遷移表。

#include "state/connection_state_machine.h"

#include <array>
#include <cstddef>

namespace vocroz::session::state {
namespace {

using S = control_protocol::ConnectionState;

constexpr std::size_t kStateCount = 6;
constexpr std::size_t kEventCount = 8;

using Row = std::array<std::optional<S>, kEventCount>;

// その状態で受け付けないイベント。
constexpr std::optional<S> kNo = std::nullopt;

// 行は状態（ConnectionState の宣言順）、列はイベント（ConnectionEvent の宣言順）。
// LOST と FAILED のストリップはオペレーターが削除するまで位置を保って残り、
// 再接続トークンを持つ子機が戻ってきたら CONNECTING へ移る（新しいストリップを作らない）。
// 遷移表として縦横をそろえて読めるよう、自動整形を止める。
// clang-format off
constexpr std::array<Row, kStateCount> kTransitions{{
    //  Approve         Established     PathFailed  ReceiveTimeout     Resumed         GraceExpired  ReconnectStarted  PeerLeft
    {S::kConnecting, kNo,            kNo,        kNo,               kNo,            kNo,          kNo,              kNo},      // PENDING
    {kNo,            S::kConnected,  S::kFailed, S::kReconnecting,  kNo,            kNo,          S::kConnecting,   S::kLost}, // CONNECTING
    {kNo,            kNo,            kNo,        S::kReconnecting,  kNo,            kNo,          S::kConnecting,   S::kLost}, // CONNECTED
    {kNo,            kNo,            kNo,        kNo,               S::kConnected,  S::kLost,     S::kConnecting,   S::kLost}, // RECONNECTING
    {kNo,            kNo,            kNo,        kNo,               kNo,            kNo,          S::kConnecting,   kNo},      // LOST
    {kNo,            kNo,            kNo,        kNo,               kNo,            kNo,          S::kConnecting,   kNo},      // FAILED
}};
// clang-format on

}  // namespace

std::optional<S> next_state(S current, ConnectionEvent event) noexcept {
    return kTransitions[static_cast<std::size_t>(current)][static_cast<std::size_t>(event)];
}

}  // namespace vocroz::session::state
