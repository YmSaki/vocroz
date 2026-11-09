// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機モードの UI（webui の MessageChannel）と ClientSession の間の変換。
// UI からの JSON を control-protocol でデコードして session へ渡し（信頼境界）、session が出す
// メッセージを JSON にエンコードして UI へ送る（session の UiEventSink の実装）。
// webui は UI からの受信をメッセージスレッドで渡すので、子機モードではメッセージスレッドを
// コントロールスレッドとして使う（docs/adr/0030-remote-pa-assembly.md）。
#pragma once

#include <functional>
#include <string_view>

#include "vocroz/common/ports/logger.h"
#include "vocroz/control_protocol/commands.h"
#include "vocroz/control_protocol/ports/message_channel.h"
#include "vocroz/control_protocol/ports/message_listener.h"
#include "vocroz/session/ports/ui_event_sink.h"

namespace vocroz::client {

/// UI と session の間の変換。
/// スレッド安全性: すべてコントロールスレッド（子機モードではメッセージスレッド）から呼ばれる。
class UiBridge final : public control_protocol::MessageListener, public session::UiEventSink {
public:
    /// デコード済みのメッセージの渡し先。ClientRuntime が ClientSession へつなぐ。
    using Handler = std::function<void(const control_protocol::UiToEngineMessage&)>;

    /// @param channel UI への経路。このオブジェクトより長く生存させる
    /// @param logger  不正なメッセージの記録先。このオブジェクトより長く生存させる
    UiBridge(control_protocol::MessageChannel& channel, common::Logger& logger);
    /// channel のリスナーを外してから破棄する。
    ~UiBridge() override;
    UiBridge(const UiBridge&) = delete;
    UiBridge& operator=(const UiBridge&) = delete;

    /// 受信を始める。handler は stop() か破棄まで呼ばれる。
    /// @param handler デコード済みのメッセージの渡し先
    void start(Handler handler);

    /// UI からのメッセージをデコードして handler へ渡す。不正なメッセージはログに残して捨てる
    /// （UI は同じプロセスの自分の画面なので、不正なら UI の不具合で、応答しても直らない）。
    void on_message(std::string_view message) override;

    /// UI の経路が閉じた。子機モードではウィンドウの破棄のときだけ起きるので、何もしない。
    void on_closed() override;

    /// session のメッセージを UI へ送る。UI が読み込み中なら捨てる（読み込み後の hello で
    /// スナップショットを受け取り直す）。
    void publish(const control_protocol::EngineToUiMessage& message) override;

private:
    control_protocol::MessageChannel& channel_;
    common::Logger& logger_;
    Handler handler_;
};

}  // namespace vocroz::client
