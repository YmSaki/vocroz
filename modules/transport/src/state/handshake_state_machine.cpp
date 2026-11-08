// SPDX-License-Identifier: AGPL-3.0-only
//
// ハンドシェイクの遷移表。

#include "state/handshake_state_machine.h"

#include <array>
#include <cstddef>

namespace vocroz::transport::state {
namespace {

constexpr std::size_t kStateCount = 7;
constexpr std::size_t kEventCount = 6;

using S = HandshakeState;
using Row = std::array<std::optional<HandshakeState>, kEventCount>;

// その状態で受け付けないイベント。
constexpr std::optional<HandshakeState> kNo = std::nullopt;

// 行は状態（HandshakeState の宣言順）、列はイベント（HandshakeEvent の宣言順）。
// 失敗（kFailed）は、まだ終わっていないすべての状態から受け付ける。
// 遷移表として縦横をそろえて読めるよう、自動整形を止める。
// clang-format off
constexpr std::array<Row, kStateCount> kTransitions{{
    //  InitWritten              InitRead                    RespWritten  RespRead    Finished      Failed
    {S::kAwaitingResponseRead, kNo,                       kNo,         kNo,        kNo,          S::kFailed},  // kAwaitingInitiationWrite
    {kNo,                      kNo,                       kNo,         S::kReady,  kNo,          S::kFailed},  // kAwaitingResponseRead
    {kNo,                      S::kAwaitingResponseWrite, kNo,         kNo,        kNo,          S::kFailed},  // kAwaitingInitiationRead
    {kNo,                      kNo,                       S::kReady,   kNo,        kNo,          S::kFailed},  // kAwaitingResponseWrite
    {kNo,                      kNo,                       kNo,         kNo,        S::kFinished, S::kFailed},  // kReady
    {kNo,                      kNo,                       kNo,         kNo,        kNo,          kNo},         // kFinished
    {kNo,                      kNo,                       kNo,         kNo,        kNo,          kNo},         // kFailed
}};
// clang-format on

}  // namespace

std::optional<HandshakeState> next_state(HandshakeState current, HandshakeEvent event) noexcept {
    return kTransitions[static_cast<std::size_t>(current)][static_cast<std::size_t>(event)];
}

}  // namespace vocroz::transport::state
