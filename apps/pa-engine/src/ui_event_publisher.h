// SPDX-License-Identifier: AGPL-3.0-only
//
// session の UiEventSink の実装。エンジンから UI へのメッセージを control-protocol の JSON に
// エンコードし、名前付きパイプの MessageChannel へ送る。
// パイプのサーバーは UI の切断を知らせないので（docs/adr/0020-ipc-pipe-and-webui-bridge.md）、
// 送信の成否と UI からの受信で「UI がつながっているか」を推定し、自動終了の判断に使う。
// メーターを 20 Hz で送り続けるので、UI が切断すれば 50 ms 以内に送信の失敗で気付ける。
#pragma once

#include "vocroz/common/ports/logger.h"
#include "vocroz/control_protocol/events.h"
#include "vocroz/control_protocol/ports/message_channel.h"
#include "vocroz/session/ports/ui_event_sink.h"

namespace vocroz::engine {

/// UI へのメッセージの送り出し。
/// スレッド安全性: すべてのメンバー関数をコントロールスレッドから呼ぶ。
class UiEventPublisher final : public session::UiEventSink {
public:
    /// @param channel UI への経路。このオブジェクトより長く生存させる
    /// @param logger  送れなかったメッセージの記録先。このオブジェクトより長く生存させる
    UiEventPublisher(control_protocol::MessageChannel& channel, common::Logger& logger) noexcept
        : channel_(channel), logger_(logger) {}

    /// メッセージをエンコードして送る。UI がつながっていなければ捨てる
    /// （UI はつながり直したときに hello でスナップショットを受け取り直す）。
    /// @param message 送るメッセージ
    void publish(const control_protocol::EngineToUiMessage& message) override;

    /// UI からメッセージが届いたことを記録する。届いた時点で UI はつながっている。
    void note_ui_message_received() noexcept {
        ui_connected_ = true;
    }

    /// UI がつながっていると推定できれば true を返す。
    /// 最後の送信が成功したか、最後の送信の失敗のあとに UI からメッセージが届いていれば true。
    [[nodiscard]] bool is_ui_connected() const noexcept {
        return ui_connected_;
    }

private:
    control_protocol::MessageChannel& channel_;
    common::Logger& logger_;
    // 起動直後は UI がまだつながっていない。
    bool ui_connected_ = false;
};

}  // namespace vocroz::engine
