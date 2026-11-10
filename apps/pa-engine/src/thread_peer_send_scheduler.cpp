// SPDX-License-Identifier: AGPL-3.0-only
//
// ThreadPeerSendScheduler の実装。

#include "thread_peer_send_scheduler.h"

#include <atomic>
#include <thread>

#include "vocroz/transport/send_wakeup.h"

namespace vocroz::engine {
namespace {

// 1 つの接続の送信スレッド。
class ThreadPeerSendWorker final : public PeerSendWorker {
public:
    ThreadPeerSendWorker() = default;
    ThreadPeerSendWorker(const ThreadPeerSendWorker&) = delete;
    ThreadPeerSendWorker& operator=(const ThreadPeerSendWorker&) = delete;

    ~ThreadPeerSendWorker() override {
        stop();
    }

    void notify() noexcept VOCROZ_NONBLOCKING override {
        wakeup_.notify();
    }

    void start(transport::PeerPacketSender& sender) override {
        if (thread_.joinable()) {
            return;
        }
        thread_ = std::thread([this, &sender] { run(sender); });
    }

    void stop() override {
        if (!thread_.joinable()) {
            return;
        }
        stopping_.store(true, std::memory_order_release);
        wakeup_.notify();
        thread_.join();
    }

private:
    void run(transport::PeerPacketSender& sender) noexcept {
        while (true) {
            // 処理の前に世代を読んでおき、処理中に入ったものの合図を取りこぼさない。
            const auto seen = wakeup_.generation();
            sender.pump();
            if (stopping_.load(std::memory_order_acquire)) {
                // 止める直前に入ったもの（閉じる前の制御メッセージなど）も送ってから終わる。
                sender.pump();
                return;
            }
            wakeup_.wait(seen);
        }
    }

    transport::SendWakeup wakeup_;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};

}  // namespace

std::unique_ptr<PeerSendWorker> ThreadPeerSendScheduler::create() {
    return std::make_unique<ThreadPeerSendWorker>();
}

}  // namespace vocroz::engine
