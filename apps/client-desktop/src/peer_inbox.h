// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機から届いたハンドシェイクの応答を、ネットワーク受信スレッドからコントロールスレッドへ渡す
// 受け箱。鍵交換はコントロールスレッド（HostConnection）が行うので、SPSC キューで渡す
// （AGENTS.md 6.1）。鍵交換の後のパケットは、受信スレッドの HostReceivePath が復号する
// （docs/adr/0036-return-monitor.md）。HostReceivePath が応答だけをここへ渡す。
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "vocroz/common/spsc_queue.h"
#include "vocroz/network/ports/datagram_listener.h"
#include "vocroz/network/udp_address.h"
#include "vocroz/transport/constants.h"

#include "client_constants.h"

namespace vocroz::client {

/// コントロールスレッドへ渡す、相手のパケット 1 つ。
struct PeerDatagram {
    /// 送信元。
    network::UdpAddress from;
    /// bytes のうち有効な長さ。
    std::size_t size = 0;
    std::array<std::uint8_t, transport::kMaxPacketBytes> bytes{};

    /// 有効な範囲を返す。
    [[nodiscard]] std::span<const std::uint8_t> view() const noexcept {
        return std::span<const std::uint8_t>(bytes).first(size);
    }
};

/// 相手のパケットの受け箱。
class PeerInbox final : public network::DatagramListener {
public:
    /// パケットをキューに入れる。kMaxPacketBytes より長いものと、満杯のときのものは捨てる
    /// （ハンドシェイクは子機が送り直す）。
    /// スレッド安全性: ネットワーク受信スレッドから呼ぶ。ヒープを確保しない。
    void on_datagram(const network::UdpAddress& from,
                     std::span<const std::uint8_t> datagram) noexcept override;

    /// パケットを 1 つ取り出す。
    /// @return 取り出したパケット。なければ std::nullopt
    /// スレッド安全性: コントロールスレッド（消費者は 1 つ）から呼ぶ。
    [[nodiscard]] std::optional<PeerDatagram> pop() noexcept {
        return inbox_.try_pop();
    }

    /// 捨てたパケットの数を返す。
    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    common::SpscQueue<PeerDatagram, kPeerInboxCapacity> inbox_;
    std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace vocroz::client
