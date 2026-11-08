// SPDX-License-Identifier: AGPL-3.0-only
//
// 出力デバイスの状態の遷移表。

#include "state/output_device_state_machine.h"

#include <array>
#include <cstddef>

namespace vocroz::session::state {
namespace {

using S = control_protocol::OutputDeviceState;

constexpr std::size_t kStateCount = 3;
constexpr std::size_t kEventCount = 3;

using Row = std::array<std::optional<S>, kEventCount>;

// その状態で受け付けないイベント。
constexpr std::optional<S> kNo = std::nullopt;

// 行は状態（OutputDeviceState の宣言順）、列はイベント（OutputDeviceEvent の宣言順）。
// 使っていないデバイスは消えようがないので、NONE と LOST では kLost を受け付けない。
// 遷移表として縦横をそろえて読めるよう、自動整形を止める。
// clang-format off
constexpr std::array<Row, kStateCount> kTransitions{{
    //  Opened    Lost      Closed
    {S::kOpen, kNo,      kNo},       // NONE
    {S::kOpen, S::kLost, S::kNone},  // OPEN
    {S::kOpen, kNo,      S::kNone},  // LOST
}};
// clang-format on

}  // namespace

std::optional<S> next_state(S current, OutputDeviceEvent event) noexcept {
    return kTransitions[static_cast<std::size_t>(current)][static_cast<std::size_t>(event)];
}

}  // namespace vocroz::session::state
