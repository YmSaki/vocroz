// SPDX-License-Identifier: AGPL-3.0-only
//
// エンコード済みのメディアフレームを送信へ渡す先のポート。
// MediaStreamEncoder がオーディオスレッドから呼び、組み立て（pa-engine の子機との接続）が
// SPSC キューでネットワークのスレッドへ渡して、暗号化して送る（docs/adr/0036-return-monitor.md）。
// 暗号化と送信をオーディオスレッドから外すのは、ソケット I/O がブロックしうるため（AGENTS.md 第 9
// 節）。
#pragma once

#include "vocroz/audio_core/realtime.h"
#include "vocroz/transport/media_frame.h"

namespace vocroz::transport {

/// エンコード済みのフレームの渡し先。
class MediaFrameSink {
public:
    virtual ~MediaFrameSink() = default;

    /// 1 フレームを渡す。
    /// @param frame エンコード済みのフレーム。呼び出しから戻った後は参照しない
    /// @return 受け取れたら true。送信待ちが満杯なら false（呼び出し側はそのフレームを捨てる）
    /// スレッド安全性: オーディオスレッドから呼び出し可能。実装はヒープを確保せず、ブロックしない。
    [[nodiscard]] virtual bool push_frame(const MediaFrame& frame) noexcept VOCROZ_NONBLOCKING = 0;
};

}  // namespace vocroz::transport
