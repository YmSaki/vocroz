// SPDX-License-Identifier: AGPL-3.0-only
//
// UDP ホールパンチの送信。PUNCH で受け取った相手のすべての候補へ、同じデータグラムを
// kPunchInterval の間隔で、最後の PUNCH から kPunchWindow の間送り続ける。
// 直結できる候補とサーバー再帰アドレスへ同時に送るので、直結とホールパンチを順に待たずに済む
// （docs/adr/0021-udp-socket-and-nat-traversal.md）。
// 何を送るかは使う側が決める（子機は鍵交換の 1 つ目のメッセージ、親機は空のデータグラム）。
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "vocroz/common/ports/clock.h"
#include "vocroz/network/ports/udp_endpoint.h"
#include "vocroz/network/rendezvous_types.h"
#include "vocroz/network/udp_address.h"

namespace vocroz::network {

/// 相手ごとのホールパンチの送信。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッド（コントロールスレッド）から使う。
class HolePuncher {
public:
    /// @param endpoint 送信に使うソケット。HolePuncher より長く生きること
    explicit HolePuncher(UdpEndpoint& endpoint) noexcept;

    /// 送るデータグラムを決める。次の送信から使う。
    /// @param datagram 送るデータグラム（空でもよい）
    void set_datagram(std::span<const std::uint8_t> datagram);

    /// 相手へのホールパンチを始めるか、送り続ける時間を延ばす。すぐに 1 回送る。
    /// 同じ相手の候補が変わっていれば置き換える。
    /// @param peer       相手のピア ID
    /// @param candidates 相手の候補（PUNCH の候補。検証済み）
    /// @param now        現在時刻
    void punch(const PeerId& peer, std::span<const UdpAddress> candidates,
               common::Clock::TimePoint now);

    /// 送信先の候補に address を含む相手へのホールパンチをやめる。
    /// 親機が、子機から認証済みのパケットを受け取ったときに使う。
    /// @param address 認証済みのパケットの送信元
    void stop_for_address(const UdpAddress& address);

    /// すべての相手へのホールパンチをやめる。
    void stop_all() noexcept;

    /// 送る時刻になった相手へ送り、送り続ける時間を過ぎた相手を外す。
    /// @param now 現在時刻
    void poll(common::Clock::TimePoint now);

    /// ホールパンチ中の相手がいれば true を返す。
    [[nodiscard]] bool active() const noexcept {
        return !targets_.empty();
    }

    /// 次に poll() を呼ぶべき時刻を返す。相手がいなければ std::nullopt。
    [[nodiscard]] std::optional<common::Clock::TimePoint> next_deadline() const noexcept;

private:
    struct Target {
        PeerId peer{};
        std::vector<UdpAddress> candidates;
        common::Clock::TimePoint window_end{};
        common::Clock::TimePoint next_send_at{};
    };

    void send_to_all(const Target& target);

    UdpEndpoint& endpoint_;
    std::vector<std::uint8_t> datagram_;
    std::vector<Target> targets_;
};

}  // namespace vocroz::network
