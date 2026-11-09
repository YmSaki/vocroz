// SPDX-License-Identifier: AGPL-3.0-only
//
// PacketSender::pump() を回す送信スレッド。送るものが入るたびに transport::SendWakeup で起こされ、
// 送信待ちを空にしてまた眠る。オーディオスレッドからソケットへ直接送らないための中継で、
// 合図で起きるのでポーリングの周期ぶんの遅延を足さない。
#pragma once

#include <atomic>
#include <thread>

#include "vocroz/transport/send_wakeup.h"

#include "packet_sender.h"

namespace vocroz::client {

/// 送信スレッド。生成するとスレッドを起動し、破棄すると止めて待つ。
/// スレッド安全性: 生成と破棄はコントロールスレッドで行う。
class SenderThread {
public:
    /// @param sender 送るパケットの持ち主。このオブジェクトより長く生存させる
    /// @param wakeup sender に渡したものと同じ合図。このオブジェクトより長く生存させる
    SenderThread(PacketSender& sender, transport::SendWakeup& wakeup);
    ~SenderThread();
    SenderThread(const SenderThread&) = delete;
    SenderThread& operator=(const SenderThread&) = delete;

private:
    void run() noexcept;

    PacketSender& sender_;
    transport::SendWakeup& wakeup_;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};

}  // namespace vocroz::client
