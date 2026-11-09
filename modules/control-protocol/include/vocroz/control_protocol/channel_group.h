// SPDX-License-Identifier: AGPL-3.0-only
//
// 複数の UI の経路を、1 本の MessageChannel にまとめる。
// 親機モードの remote-pa で、ミキサーの窓とルーティングの窓を、
// pa-engine への 1 本の名前付きパイプにつなぐために使う（docs/adr/0038-routing-window.md）。
// pa-engine のパイプは、同時につながる UI を 1 つに限っている。
// そのため窓ごとに接続を増やさず、ここで多重化する。
// 中継と同じく、メッセージはデコードしない。
// エンジンからのメッセージはすべての窓に配り、どの窓からのメッセージもそのままエンジンへ渡す。
// 応答の振り分けは、窓ごとに違う request_id の接頭辞で UI が行う。
#pragma once

#include <mutex>
#include <string_view>
#include <vector>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/ports/message_channel.h"
#include "vocroz/control_protocol/ports/message_listener.h"

namespace vocroz::control_protocol {

/// 複数の経路を 1 本に見せる MessageChannel。
/// send() はすべての経路へ送り、受信はどの経路からも同じリスナーに渡す。
/// 経路の 1 つが閉じても、まとめた経路全体は閉じたとみなさない（窓を閉じただけなので）。
/// 閉じた経路は、持ち主が remove() で外す。
///
/// スレッド安全性:
/// - send() はオーディオスレッド以外のどのスレッドからも同時に呼び出し可能。
/// - add()、remove()、set_listener() はオーディオスレッド以外のどのスレッドからも呼び出し可能。
/// - まとめる経路の受信は、同時に呼ばれないこと。
///   MessageListener は同時に呼ばれない前提のため。
///   WebView のブリッジは、どれもメッセージスレッドで受信するので満たす。
class ChannelGroup final : public MessageChannel {
public:
    ChannelGroup() = default;
    /// まとめている経路のリスナーを外す。
    ~ChannelGroup() override;

    ChannelGroup(const ChannelGroup&) = delete;
    ChannelGroup& operator=(const ChannelGroup&) = delete;
    ChannelGroup(ChannelGroup&&) = delete;
    ChannelGroup& operator=(ChannelGroup&&) = delete;

    /// 経路を加える。設定済みのリスナーがあれば、その経路にも設定する。
    /// @param member 加える経路。remove() するか、このオブジェクトが破棄されるまで生存させる
    void add(MessageChannel& member);

    /// 経路を外し、そのリスナーを外す。戻った後は、その経路への send() と、その経路からの受信の
    /// 呼び出しは実行中でない。加えていない経路なら何もしない。
    /// @param member 外す経路
    void remove(MessageChannel& member);

    /// すべての経路へ送る。
    /// @param message 1 メッセージ分の JSON
    /// @return 1 つ以上の経路が受け付けたら成功。経路がなければ kClosed。
    ///         すべての経路が失敗したら、最後の経路の原因
    [[nodiscard]] common::Result<void, ChannelError> send(std::string_view message) override;

    /// すべての経路のリスナーを設定する。nullptr で解除する。
    void set_listener(MessageListener* listener) override;

private:
    // send() と add()、remove() の間の排他。経路の send() はブロックしないので、
    // 持ったまま呼んでよい。
    std::mutex mutex_;
    std::vector<MessageChannel*> members_;
    MessageListener* listener_ = nullptr;
};

}  // namespace vocroz::control_protocol
