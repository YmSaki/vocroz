// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の DatagramListener。受け取ったデータグラムを記録する。
// ネットワーク受信スレッドから呼ばれても、テストのスレッドが待って取り出せるよう、ロックで守る。
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

#include "vocroz/network/ports/datagram_listener.h"

namespace vocroz::network {

/// 受け取ったデータグラムを記録する DatagramListener。
class FakeDatagramListener final : public DatagramListener {
public:
    /// 受け取ったデータグラム 1 つ。
    struct Received {
        UdpAddress from;
        std::vector<std::uint8_t> datagram;
    };

    void on_datagram(const UdpAddress& from,
                     std::span<const std::uint8_t> datagram) noexcept override {
        const std::lock_guard lock(mutex_);
        received_.push_back(Received{from, {datagram.begin(), datagram.end()}});
        changed_.notify_all();
    }

    /// count 個以上受け取るまで、最大 timeout 待つ。
    /// @return 受け取ったデータグラムの写し
    [[nodiscard]] std::vector<Received> wait_for(std::size_t count,
                                                 std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        changed_.wait_for(lock, timeout, [&] { return received_.size() >= count; });
        return received_;
    }

    /// これまでに受け取ったデータグラムの写しを返す。
    [[nodiscard]] std::vector<Received> received() {
        const std::lock_guard lock(mutex_);
        return received_;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<Received> received_;
};

}  // namespace vocroz::network
