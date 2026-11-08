// SPDX-License-Identifier: AGPL-3.0-only
//
// 参加者の接続が CONNECTED のときの信号状態。
// 正規名は仕様書の状態モデルの名前に合わせる。
// 接続状態（session）やミュートフラグとは別の軸として持つ。
#pragma once

#include <cstdint>
#include <string_view>

namespace vocroz::transport {

/// 信号状態。
/// 接続状態が CONNECTED 以外のときは意味を持たない。
enum class SignalState : std::uint8_t {
    kReceiving,  ///< RECEIVING: 有効な音声フレームを受信中（親機の channel_muted 中も含む）
    kPaused,  ///< PAUSED: 子機の送信ミュートにより意図的に停止中
    kResuming,  ///< RESUMING: 送信ミュートの解除通知のあと、最初のフレーム待ち
    kNoSignal,  ///< NO_SIGNAL: 送信中のはずがフレームが届かない。無音化済み
};

/// 信号状態の正規名（プロトコル、ログ、テストで使う名前）を返す。
/// UI には表示しない。
/// @param state 信号状態
/// @return "RECEIVING" などの正規名
/// スレッド安全性: どのスレッドからも呼び出し可能。
[[nodiscard]] constexpr std::string_view canonical_name(SignalState state) noexcept {
    switch (state) {
        case SignalState::kReceiving:
            return "RECEIVING";
        case SignalState::kPaused:
            return "PAUSED";
        case SignalState::kResuming:
            return "RESUMING";
        case SignalState::kNoSignal:
            return "NO_SIGNAL";
    }
    return "NO_SIGNAL";  // 列挙子以外の値は来ないが、未定義の値は無音側（NO_SIGNAL）として扱う
}

}  // namespace vocroz::transport
