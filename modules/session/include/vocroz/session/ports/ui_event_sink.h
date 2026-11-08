// SPDX-License-Identifier: AGPL-3.0-only
//
// エンジンから UI へのメッセージ（スナップショット、差分イベント、応答、通知）を送り出すポート。
// コンポジションルートが control-protocol の JSON にエンコードして MessageChannel へ流す。
// session は送り先（名前付きパイプか、プロセス内の WebView か）を知らずに済む。
#pragma once

#include "vocroz/control_protocol/events.h"

namespace vocroz::session {

/// UI へのメッセージの送り先。
class UiEventSink {
public:
    virtual ~UiEventSink() = default;

    /// メッセージを 1 つ送る。UI が接続していなければ捨ててよい（UI は接続時にスナップショットを
    /// 受け取り直すため）。
    /// @param message 送るメッセージ
    /// スレッド安全性: コントロールスレッドから呼ぶ。実装はブロックしない。
    virtual void publish(const control_protocol::EngineToUiMessage& message) = 0;
};

}  // namespace vocroz::session
