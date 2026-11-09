// SPDX-License-Identifier: AGPL-3.0-only
//
// SenderThread の実装。

#include "sender_thread.h"

namespace vocroz::client {

SenderThread::SenderThread(PacketSender& sender, transport::SendWakeup& wakeup)
    : sender_(sender), wakeup_(wakeup), thread_([this] { run(); }) {}

SenderThread::~SenderThread() {
    stopping_.store(true, std::memory_order_release);
    wakeup_.notify();
    thread_.join();
}

void SenderThread::run() noexcept {
    while (true) {
        // 処理の前に世代を読んでおき、処理中に入ったものの合図を取りこぼさない。
        const auto seen = wakeup_.generation();
        sender_.pump();
        if (stopping_.load(std::memory_order_acquire)) {
            // 止める直前に入ったもの（終了時の退出の通知など）も送ってから終わる。
            sender_.pump();
            return;
        }
        wakeup_.wait(seen);
    }
}

}  // namespace vocroz::client
