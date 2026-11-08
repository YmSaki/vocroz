// SPDX-License-Identifier: AGPL-3.0-only
//
// 2 つの MessageChannel の間でメッセージをそのまま中継する。
// 親機モードの remote-pa で、WebView のブリッジ（webui）と pa-engine
// への名前付きパイプ（ipc）をつなぐ。 中継ではデコードせず、検証は受け取る側（エンジン）で 1
// 回だけ行う（中継の遅れとコピーを最小にするため）。
#pragma once

#include <functional>

#include "vocroz/control_protocol/ports/message_channel.h"
#include "vocroz/control_protocol/ports/message_listener.h"

namespace vocroz::control_protocol {

/// 2 つの MessageChannel の間の中継。
/// 生成すると両方の経路にリスナーを設定し、破棄すると解除する。
/// スレッド安全性: メッセージは各経路の受信スレッドで相手の経路の send() に渡す。
/// 生成と破棄は、両方の経路の set_listener() を呼べるスレッドで行う。
class ChannelRelay {
public:
    /// 中継する経路の区別。
    enum class Side {
        kUi,      ///< UI 側（webui）
        kEngine,  ///< エンジン側（ipc）
    };

    /// 経路が閉じたときに呼ばれる関数。閉じた側を受け取る。閉じた経路の受信スレッドから呼ばれる。
    /// 例: エンジン側が閉じたら、コンポジションルートがエンジンの異常終了を利用者に知らせる。
    using ClosedHandler = std::function<void(Side closed)>;

    /// 中継を始める。
    /// @param ui        UI 側の経路。中継より長く生存させる
    /// @param engine    エンジン側の経路。中継より長く生存させる
    /// @param on_closed 経路が閉じたときに呼ぶ関数。空でもよい
    ChannelRelay(MessageChannel& ui, MessageChannel& engine, ClosedHandler on_closed);

    /// 両方の経路のリスナーを解除する。戻った後は中継のコールバックは実行されない。
    ~ChannelRelay();

    ChannelRelay(const ChannelRelay&) = delete;
    ChannelRelay& operator=(const ChannelRelay&) = delete;
    ChannelRelay(ChannelRelay&&) = delete;
    ChannelRelay& operator=(ChannelRelay&&) = delete;

private:
    /// 一方の経路の受信を、もう一方の経路へ送る。
    class Forwarder final : public MessageListener {
    public:
        Forwarder(MessageChannel& destination, Side side, const ClosedHandler& on_closed);
        void on_message(std::string_view message) override;
        void on_closed() override;

    private:
        MessageChannel& destination_;
        const Side side_;
        const ClosedHandler& on_closed_;
    };

    MessageChannel& ui_;
    MessageChannel& engine_;
    const ClosedHandler on_closed_;
    Forwarder from_ui_;
    Forwarder from_engine_;
};

}  // namespace vocroz::control_protocol
