// SPDX-License-Identifier: AGPL-3.0-only
//
// 送るものを送信待ちに入れたことを、送信する側（送信スレッド）に知らせるポート。
// 送信待ちに入れる側（オーディオスレッドとコントロールスレッド）が、入れた直後に呼ぶ。
// 実装は送信スレッドを起こす合図（SendWakeup）で、テストではその場で送るものに差し替える。
#pragma once

#include "vocroz/audio_core/realtime.h"

namespace vocroz::transport {

/// 送るものが入ったことの通知先。
class SendNotifier {
public:
    virtual ~SendNotifier() = default;

    /// 送るものが入ったことを知らせる。
    /// スレッド安全性: どのスレッドからも同時に呼び出し可能。オーディオスレッドから呼び出し可能。
    /// 実装はヒープを確保せず、ブロックしない。
    virtual void notify() noexcept VOCROZ_NONBLOCKING = 0;
};

}  // namespace vocroz::transport
