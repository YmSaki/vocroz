// SPDX-License-Identifier: AGPL-3.0-only
//
// UI とエンジン間のメッセージを運ぶ双方向の経路のポート。
// 名前付きパイプ（ipc）と WebView のブリッジ（webui）が実装し、テストでは FakeMessageChannel
// に差し替える。 1 回の send() と on_message() が 1
// メッセージに対応し、フレーミング（長さの前置きなど）は実装が受け持つ。
#pragma once

#include <string_view>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/ports/message_listener.h"

namespace vocroz::control_protocol {

/// send() の失敗の原因。
enum class ChannelError {
    kClosed,    ///< 経路が閉じている
    kTooLarge,  ///< メッセージが kMaxMessageBytes を超えている
};

/// UI とエンジン間の双方向のメッセージ経路。
/// 実装はコンポジションルートで生成し、参照で注入する（AGENTS.md 4.3）。
class MessageChannel {
public:
    virtual ~MessageChannel() = default;

    /// メッセージを 1 つ送る。相手に届いたかは返さない（応答はメッセージで受け取る）。
    /// @param message 1 メッセージ分の JSON。呼び出しから戻った後は参照されない
    /// @return 送信を受け付けたら成功。閉じた経路や大きすぎるメッセージは失敗
    /// スレッド安全性: オーディオスレッド以外のどのスレッドからも同時に呼び出し可能。
    /// 送った順に相手に届く。
    [[nodiscard]] virtual common::Result<void, ChannelError> send(std::string_view message) = 0;

    /// 受信を受け取るリスナーを設定する。
    /// @param listener 受け取るリスナー。nullptr
    /// で解除する。呼び出し側が所有し、解除するまで生存させる
    /// 戻った後は、以前のリスナーの呼び出しが実行中でないことを実装が保証する
    /// （解除の直後にリスナーを破棄できるようにするため）。リスナーの中からは呼び出さない。
    /// スレッド安全性: オーディオスレッド以外のどのスレッドからも呼び出し可能。
    virtual void set_listener(MessageListener* listener) = 0;
};

}  // namespace vocroz::control_protocol
