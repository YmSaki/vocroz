// SPDX-License-Identifier: AGPL-3.0-only
//
// WebView のブリッジの MessageChannel。JSON の文字列を中身を読まずに中継する。
// WebView を操作する部分（UiEventSink）と分け、送信の可否、大きさの上限、閉じた後の扱いを
// JUCE なしで単体テストできるようにする（docs/adr/0020-ipc-pipe-and-webui-bridge.md）。
#pragma once

#include <mutex>
#include <string>
#include <string_view>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/listener_slot.h"
#include "vocroz/control_protocol/ports/message_channel.h"

namespace vocroz::webui {

/// ネイティブ側から UI へメッセージを届ける先。JUCE の実装は、メッセージスレッドに渡してから
/// WebView にイベントとして送る。
class UiEventSink {
public:
    virtual ~UiEventSink() = default;

    /// メッセージを UI に届けるよう依頼する。届いたかは返さない。
    /// @param message 1 メッセージ分の JSON
    /// スレッド安全性: オーディオスレッド以外のどのスレッドからも呼ばれる。ブロックしない。
    virtual void post_to_ui(std::string message) = 0;
};

/// WebView のブリッジ。
/// UI からの受信は、receive_from_ui() を呼んだスレッド（JUCE
/// のメッセージスレッド）でリスナーに渡す。
class BridgeChannel final : public control_protocol::MessageChannel {
public:
    /// @param sink UI にメッセージを届ける先。close() を呼ぶまで生存させる
    explicit BridgeChannel(UiEventSink& sink);

    /// UI にメッセージを送る。close() の後は kClosed、上限を超えるメッセージは kTooLarge。
    [[nodiscard]] common::Result<void, control_protocol::ChannelError> send(
        std::string_view message) override;

    void set_listener(control_protocol::MessageListener* listener) override;

    /// UI から届いたメッセージをリスナーに渡す。
    /// 上限を超えるメッセージは、UI の不具合か改ざんなので渡さずに捨てる。
    /// @return リスナーに渡したら true、捨てたら false
    bool receive_from_ui(std::string_view message);

    /// 閉じる。戻った後は、send() が sink を呼ばない（sink を破棄できる）。リスナーは呼ばない。
    /// スレッド安全性: どのスレッドからも呼び出し可能。何度呼んでもよい。
    void close();

private:
    UiEventSink& sink_;
    std::mutex sink_mutex_;  ///< close() の後に sink_ を呼ばないための排他
    bool closed_ = false;
    control_protocol::MessageListenerSlot listener_;
};

}  // namespace vocroz::webui
