// SPDX-License-Identifier: AGPL-3.0-only
//
// Noise_IK のハンドシェイクの進行を表す有限状態機械（AGENTS.md 6.1）。
// メッセージの読み書きの順序を遷移表 1 か所で決め、順序を誤った呼び出しを kInvalidState で拒む。
// 副作用（noise-c の呼び出し）は NoiseIkHandshake が行う。
#pragma once

#include <cstdint>
#include <optional>

namespace vocroz::transport::state {

/// ハンドシェイクの状態。
enum class HandshakeState : std::uint8_t {
    /// 開始側: 1 つ目のメッセージを送る前。
    kAwaitingInitiationWrite,
    /// 開始側: 1 つ目を送り、2 つ目のメッセージを待っている。
    kAwaitingResponseRead,
    /// 応答側: 1 つ目のメッセージを待っている。
    kAwaitingInitiationRead,
    /// 応答側: 1 つ目を読み、2 つ目のメッセージを送る前。
    kAwaitingResponseWrite,
    /// 両側: ハンドシェイクが完了し、鍵を取り出せる。
    kReady,
    /// 両側: 鍵を取り出し終えた。以降は何もできない。
    kFinished,
    /// 両側: 失敗した。以降は何もできない。
    kFailed,
};

/// ハンドシェイクの状態を進めるイベント。
enum class HandshakeEvent : std::uint8_t {
    /// 1 つ目のメッセージを書いた。
    kInitiationWritten,
    /// 1 つ目のメッセージを読んだ。
    kInitiationRead,
    /// 2 つ目のメッセージを書いた。
    kResponseWritten,
    /// 2 つ目のメッセージを読んだ。
    kResponseRead,
    /// 鍵を取り出した。
    kFinished,
    /// Noise の処理が失敗した。
    kFailed,
};

/// 状態 current でイベント event が起きたときの次の状態を返す。
/// @return 次の状態。その状態で受け付けないイベントなら std::nullopt
/// スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。
[[nodiscard]] std::optional<HandshakeState> next_state(HandshakeState current,
                                                       HandshakeEvent event) noexcept;

}  // namespace vocroz::transport::state
