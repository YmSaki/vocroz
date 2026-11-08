// SPDX-License-Identifier: AGPL-3.0-only
//
// MessageChannel から届いたメッセージを受け取るリスナー。
// 経路（名前付きパイプ、WebView のブリッジ）の実装が呼び出し、受け取る側（エンジン、ChannelRelay
// など）が実装する。
#pragma once

#include <string_view>

namespace vocroz::control_protocol {

/// MessageChannel の受信を受け取るリスナー。
/// スレッド安全性: 呼び出しは経路の受信スレッドから 1 つずつ行われ、同時には呼ばれない。
/// 実装はブロックする処理を長く続けない（後続のメッセージの受信が遅れるため）。
class MessageListener {
public:
    virtual ~MessageListener() = default;

    /// メッセージを 1 つ受け取る。
    /// @param message 1 メッセージ分の JSON。未検証で、呼び出しから戻った後は参照されない
    virtual void on_message(std::string_view message) = 0;

    /// 経路が閉じた（相手のプロセスの終了、パイプの切断など）。以降 on_message() は呼ばれない。
    virtual void on_closed() = 0;
};

}  // namespace vocroz::control_protocol
