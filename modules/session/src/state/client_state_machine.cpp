// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機の接続状態の遷移表。

#include "state/client_state_machine.h"

#include <array>
#include <cstddef>

namespace vocroz::session::state {
namespace {

using S = control_protocol::ClientState;

constexpr std::size_t kStateCount = 8;
constexpr std::size_t kEventCount = 10;

using Row = std::array<std::optional<S>, kEventCount>;

// その状態で受け付けないイベント。
constexpr std::optional<S> kNo = std::nullopt;

// 行は状態（ClientState の宣言順）、列はイベント（ClientEvent の宣言順）。
// 承認前（AUTHENTICATING、WAITING_APPROVAL）は再接続トークンを持たないので、途絶は kLinkLost
// ではなく kFailed（UNREACHABLE）として扱う。どれを送るかは ClientSession が決める。
// 遷移表として縦横をそろえて読めるよう、自動整形を止める。
// clang-format off
constexpr std::array<Row, kStateCount> kTransitions{{
    //  Join          Resolved            JoinPending            Approved          MediaEstablished  LinkLost           Reconnected     Failed      Leave              SessionEnded
    {S::kResolving, kNo,                kNo,                   kNo,              kNo,              kNo,               kNo,            kNo,        S::kDisconnected,  kNo},               // DISCONNECTED
    {kNo,           S::kAuthenticating, kNo,                   kNo,              kNo,              kNo,               kNo,            S::kFailed, S::kDisconnected,  kNo},               // RESOLVING
    {kNo,           kNo,                S::kWaitingApproval,   S::kConnecting,   kNo,              kNo,               kNo,            S::kFailed, S::kDisconnected,  S::kDisconnected},  // AUTHENTICATING
    {kNo,           kNo,                kNo,                   S::kConnecting,   kNo,              kNo,               kNo,            S::kFailed, S::kDisconnected,  S::kDisconnected},  // WAITING_APPROVAL
    {kNo,           kNo,                kNo,                   kNo,              S::kConnected,    S::kReconnecting,  kNo,            S::kFailed, S::kDisconnected,  S::kDisconnected},  // CONNECTING
    {kNo,           kNo,                kNo,                   kNo,              kNo,              S::kReconnecting,  kNo,            S::kFailed, S::kDisconnected,  S::kDisconnected},  // CONNECTED
    {kNo,           kNo,                kNo,                   kNo,              kNo,              kNo,               S::kConnected,  S::kFailed, S::kDisconnected,  S::kDisconnected},  // RECONNECTING
    {S::kResolving, kNo,                kNo,                   kNo,              kNo,              kNo,               kNo,            kNo,        S::kDisconnected,  kNo},               // FAILED
}};
// clang-format on

}  // namespace

std::optional<S> next_state(S current, ClientEvent event) noexcept {
    return kTransitions[static_cast<std::size_t>(current)][static_cast<std::size_t>(event)];
}

}  // namespace vocroz::session::state
