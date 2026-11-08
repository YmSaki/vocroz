// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の出力デバイスの状態（OutputDeviceState）の有限状態機械（AGENTS.md 6.1）。
// 使用中のデバイスが消えたら LOST に移り、オペレーターが新しい出力先を選ぶまで他のデバイスへ
// 切り替えない。Master の SAFETY ミュートは HostSession が LOST への遷移に合わせて行う。
#pragma once

#include <cstdint>
#include <optional>

#include "vocroz/control_protocol/states.h"

namespace vocroz::session::state {

/// 出力デバイスの状態を進めるイベント。
enum class OutputDeviceEvent : std::uint8_t {
    kOpened,  ///< 選択したデバイスを開いた
    kLost,    ///< 使用中のデバイスが抜去されたか、消えた
    kClosed,  ///< デバイスの選択を解除した
};

/// 状態 current でイベント event が起きたときの次の状態を返す。
/// @return 次の状態。その状態で受け付けないイベントなら std::nullopt
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] std::optional<control_protocol::OutputDeviceState> next_state(
    control_protocol::OutputDeviceState current, OutputDeviceEvent event) noexcept;

}  // namespace vocroz::session::state
