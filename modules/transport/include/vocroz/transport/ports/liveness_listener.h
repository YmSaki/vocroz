// SPDX-License-Identifier: AGPL-3.0-only
//
// 相手からの受信の途絶と再開を外側（session）に知らせるリスナー（AGENTS.md 4.1）。
// 接続状態を RECONNECTING に移すかどうかの判断と、再接続の実行は外側が担う。
#pragma once

namespace vocroz::transport {

/// 受信の途絶と再開の受け取り手。
class LivenessListener {
public:
    virtual ~LivenessListener() = default;

    /// 音声と制御のどちらも、受信が途絶したとみなす時間（5 秒）を超えて届かなかった。
    /// 途絶が続いている間に重ねては呼ばれない。
    /// スレッド安全性: LivenessMonitor::poll() を呼んだスレッドから、その呼び出しの中で呼ばれる。
    virtual void on_peer_timed_out() = 0;

    /// on_peer_timed_out() の後に、同じ接続のパケットがまた届いた。
    /// 経路が一時的に切れていただけで、NAT のマッピングと鍵がそのまま使える場合にあたる。
    /// スレッド安全性: LivenessMonitor::poll() を呼んだスレッドから、その呼び出しの中で呼ばれる。
    virtual void on_peer_resumed() = 0;
};

}  // namespace vocroz::transport
