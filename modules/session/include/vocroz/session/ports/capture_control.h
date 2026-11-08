// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機のキャプチャと送信へ、入力デバイス、送信ミュート、送信レベルを反映するポート。
// コンポジションルートが capture と transport のエンコーダーへ渡す。
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace vocroz::session {

/// 子機のキャプチャと送信の操作。
/// スレッド安全性: コントロールスレッドから呼ぶ。実装はブロックせず、値をオーディオスレッドへ渡す。
class CaptureControl {
public:
    virtual ~CaptureControl() = default;

    /// 入力デバイスを選ぶ。選んでも送信は始めない（送信ミュートは別に反映する）。
    /// @param device_id デバイスの永続 ID。std::nullopt は「入力なし」
    virtual void select_input(const std::optional<std::string>& device_id) = 0;

    /// 送信ミュートを反映する。ミュート中はフレームを送らない（出演者の音声を PC の外へ出さない）。
    /// 解除後に送るフレームには epoch を載せ、親機がミュート前の古いフレームを見分けられるようにする。
    /// @param muted ミュートするなら true
    /// @param epoch ミュートの世代番号。解除のたびに 1 増える
    virtual void set_send_muted(bool muted, std::uint32_t epoch) = 0;

    /// 送信レベルを反映する。5〜20 ms のゲインランプで適用する。
    /// @param level_db 送信レベル（-60〜+12 dB）
    virtual void set_send_level(double level_db) = 0;
};

}  // namespace vocroz::session
