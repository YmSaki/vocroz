// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機との接続ごとの送信（PeerPacketSender::pump()）をどこで回すかのポート。
// 本番は接続ごとの送信スレッド（ThreadPeerSendScheduler）で回し、テストは送信待ちに入った
// その場で回すものに差し替えて、送ったデータグラムを決定的に確かめられるようにする
// （docs/adr/0036-return-monitor.md）。
#pragma once

#include <memory>

#include "vocroz/transport/peer_packet_sender.h"
#include "vocroz/transport/ports/send_notifier.h"

namespace vocroz::engine {

/// 1 つの接続の送信を回すもの。PeerPacketSender の通知先（SendNotifier）を兼ねる。
/// スレッド安全性: 生成、start()、stop()、破棄はコントロールスレッドで行う。notify() はどのスレッド
/// からも呼び出し可能で、止めた後に呼んでも何もしない。
class PeerSendWorker : public transport::SendNotifier {
public:
    /// 送信を始める。以後、notify() のたびに sender.pump() を回す。
    /// @param sender 送信待ちの持ち主。stop() までは生存させる
    virtual void start(transport::PeerPacketSender& sender) = 0;

    /// 送信を止める。止める前に、送信待ちに残ったものを送り切る。2 回目以降は何もしない。
    /// 破棄でも止まるが、送信に使うソケットを先に閉じるときは、その前にこれを呼ぶ。
    virtual void stop() = 0;
};

/// 接続ごとの送信を回すものを作る。
/// スレッド安全性: コントロールスレッドから呼ぶ。
class PeerSendScheduler {
public:
    virtual ~PeerSendScheduler() = default;

    /// 新しい接続の送信を回すものを作る。start() を呼ぶまでは何も回さない。
    [[nodiscard]] virtual std::unique_ptr<PeerSendWorker> create() = 0;
};

}  // namespace vocroz::engine
