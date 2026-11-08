// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機のセッション状態の遷移表。

#include "state/session_state_machine.h"

#include <array>
#include <cstddef>

namespace vocroz::session::state {
namespace {

using S = control_protocol::SessionState;

constexpr std::size_t kStateCount = 6;
constexpr std::size_t kEventCount = 8;

using Row = std::array<std::optional<S>, kEventCount>;

// その状態で受け付けないイベント。
constexpr std::optional<S> kNo = std::nullopt;

// 行は状態（SessionState の宣言順）、列はイベント（SessionEvent の宣言順）。
// CREATING 中の終了は受け付けない。ランデブーサーバーの応答（成功か失敗）を待ってから終了させ、
// 登録された招待を確実に失効させるため。
// 遷移表として縦横をそろえて読めるよう、自動整形を止める。
// clang-format off
constexpr std::array<Row, kStateCount> kTransitions{{
    //  Create        Created    ParticipantActive  NoParticipantActive  End          Ended     Fault      Recover
    {S::kCreating, kNo,        kNo,               kNo,                 kNo,         kNo,      kNo,       kNo},      // IDLE
    {kNo,          S::kReady,  kNo,               kNo,                 kNo,         kNo,      S::kError, kNo},      // CREATING
    {kNo,          kNo,        S::kActive,        kNo,                 S::kEnding,  kNo,      S::kError, kNo},      // READY
    {kNo,          kNo,        kNo,               S::kReady,           S::kEnding,  kNo,      S::kError, kNo},      // ACTIVE
    {kNo,          kNo,        kNo,               kNo,                 kNo,         S::kIdle, kNo,       kNo},      // ENDING
    {kNo,          kNo,        kNo,               kNo,                 kNo,         kNo,      kNo,       S::kIdle}, // ERROR
}};
// clang-format on

}  // namespace

std::optional<S> next_state(S current, SessionEvent event) noexcept {
    return kTransitions[static_cast<std::size_t>(current)][static_cast<std::size_t>(event)];
}

}  // namespace vocroz::session::state
