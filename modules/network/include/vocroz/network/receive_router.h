// SPDX-License-Identifier: AGPL-3.0-only
//
// 1 本の UDP ソケットで受信したデータグラムを、ランデブーメッセージと相手のパケットに振り分ける。
// ランデブーサーバーとの通信と相手との通信は同じソケットで行う（docs/adr/0021-udp-socket-and-nat-traversal.md）。
// 相手のパケットは受信スレッドのまま transport 側へ渡し、遅延を足さない。
// ランデブーメッセージは状態を持つ処理（ClientRendezvous、HostRendezvous）がコントロールスレッドで扱うので、
// SPSC キューでコントロールスレッドへ渡す（AGENTS.md 6.1）。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "vocroz/common/spsc_queue.h"
#include "vocroz/network/ports/datagram_listener.h"
#include "vocroz/network/udp_address.h"

namespace vocroz::network {

/// コントロールスレッドへ渡すランデブーメッセージ 1 つの最大長（バイト）。PUNCH の最大長（218
/// バイト）を収める。
inline constexpr std::size_t kMaxRendezvousDatagramBytes = 256;

/// コントロールスレッドへ渡すランデブーメッセージ。
struct RendezvousDatagram {
    /// 送信元。ランデブーサーバーのアドレスと一致するかは受け取る側が確かめる。
    UdpAddress from;
    /// bytes のうち有効な長さ。
    std::size_t size = 0;
    std::array<std::uint8_t, kMaxRendezvousDatagramBytes> bytes{};

    /// 有効な範囲を返す。
    [[nodiscard]] std::span<const std::uint8_t> view() const noexcept {
        return std::span<const std::uint8_t>(bytes).first(size);
    }
};

/// 受信したデータグラムの振り分け。
/// 先頭が "VRZ" のものはランデブーメッセージとしてキューに入れ、空のものは捨て、
/// それ以外は相手のパケットとして peer_listener へ渡す。
/// 空のデータグラムは、親機がホールパンチで NAT
/// のマッピングを開くために送るもので、中身を持たない。
class ReceiveRouter final : public DatagramListener {
public:
    /// @param peer_listener 相手のパケットの渡し先。ReceiveRouter より長く生きること
    explicit ReceiveRouter(DatagramListener& peer_listener) noexcept;

    /// 受信したデータグラムを振り分ける。キューが満杯ならランデブーメッセージを捨てる
    /// （REGISTER の再送で回復する）。kMaxRendezvousDatagramBytes
    /// より長いランデブーメッセージも捨てる。 スレッド安全性:
    /// ネットワーク受信スレッドから呼ぶ（UdpEndpoint の実装が呼ぶ）。ヒープを確保しない。
    void on_datagram(const UdpAddress& from,
                     std::span<const std::uint8_t> datagram) noexcept override;

    /// キューからランデブーメッセージを 1 つ取り出す。
    /// @return 取り出したメッセージ。なければ std::nullopt
    /// スレッド安全性: コントロールスレッド（キューの消費者は 1 つ）から呼ぶ。
    [[nodiscard]] std::optional<RendezvousDatagram> pop_rendezvous() noexcept;

private:
    // ネットワーク受信スレッドからコントロールスレッドへ渡す数の上限。REGISTERED と PUNCH は
    // 1 秒に数個しか届かないので、コントロールスレッドが少し遅れても溢れない。
    static constexpr std::size_t kInboxCapacity = 32;

    DatagramListener& peer_listener_;
    common::SpscQueue<RendezvousDatagram, kInboxCapacity> inbox_;
};

}  // namespace vocroz::network
