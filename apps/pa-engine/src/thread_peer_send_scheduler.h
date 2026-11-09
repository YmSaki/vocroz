// SPDX-License-Identifier: AGPL-3.0-only
//
// 接続ごとに送信スレッドを 1 本立てる PeerSendScheduler。送るものが入るたびに SendWakeup で
// 起こされ、送信待ちを空にしてまた眠る。オーディオスレッドからソケットへ直接送らないための
// 中継で、合図で起きるのでポーリングの周期ぶんの遅延を足さない。
//
// 全接続で 1 本を共有せず接続ごとに分けるのは、ある接続の送信が詰まったときに他の子機への
// 返送まで遅らせないため。子機の数（数台から十数台）なら、眠っている時間がほとんどのスレッドが
// その数だけ増えても負荷は小さい（docs/adr/0036-return-monitor.md）。
#pragma once

#include <memory>

#include "peer_send_scheduler.h"

namespace vocroz::engine {

/// 接続ごとの送信スレッドを作る。
/// スレッド安全性: コントロールスレッドから使う。
class ThreadPeerSendScheduler final : public PeerSendScheduler {
public:
    [[nodiscard]] std::unique_ptr<PeerSendWorker> create() override;
};

}  // namespace vocroz::engine
