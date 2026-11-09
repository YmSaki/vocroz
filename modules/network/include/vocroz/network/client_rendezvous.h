// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機の、親機への経路の確立。ランデブーサーバーに登録し、PUNCH で受け取った親機のすべての候補へ
// 鍵交換の 1 つ目のメッセージを送り続ける（docs/adr/0021-udp-socket-and-nat-traversal.md）。
// 最初に認証済みの応答を返した送信元を、組み立て側が以降の送信先にして mark_established() を呼ぶ。
// 10 秒で確立できなければ、原因を判定して ClientRendezvousListener に知らせる。
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "vocroz/common/ports/clock.h"
#include "vocroz/network/network_error.h"
#include "vocroz/network/ports/client_rendezvous_listener.h"
#include "vocroz/network/ports/udp_endpoint.h"
#include "vocroz/network/receive_router.h"
#include "vocroz/network/rendezvous_types.h"
#include "vocroz/network/udp_address.h"
#include "vocroz/transport/ports/random_source.h"

namespace vocroz::network {

/// 子機の経路の確立の要求。
struct ClientRendezvousRequest {
    /// ランデブーサーバーのアドレス（select_server_address() で選ぶ）。
    UdpAddress server;
    /// 招待で受け取ったランデブー ID。
    RendezvousId rendezvous_id{};
    /// 申告するローカル候補（select_local_candidates() で選ぶ）。
    std::vector<UdpAddress> local_candidates;
    /// 親機の候補へ送るデータグラム。Noise_IK の 1 つ目のメッセージ。
    std::vector<std::uint8_t> opening_datagram;
};

/// 子機の経路の確立。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッド（コントロールスレッド）から使う。
class ClientRendezvous {
public:
    /// @param endpoint 送信に使うソケット。ClientRendezvous より長く生きること
    /// @param random   ピア ID とトランザクション ID に使う乱数。ClientRendezvous
    /// より長く生きること
    /// @param listener 失敗の受け取り手。ClientRendezvous より長く生きること
    ClientRendezvous(UdpEndpoint& endpoint, transport::RandomSource& random,
                     ClientRendezvousListener& listener);
    ~ClientRendezvous();
    ClientRendezvous(const ClientRendezvous&) = delete;
    ClientRendezvous& operator=(const ClientRendezvous&) = delete;

    /// 経路の確立を始める。試行中なら最初からやり直す（ピア ID も新しくする）。
    /// @param request 要求
    /// @param now     現在時刻
    void start(const ClientRendezvousRequest& request, common::Clock::TimePoint now);

    /// ReceiveRouter から取り出したランデブーメッセージを渡す。
    /// @param datagram メッセージ
    /// @param now      現在時刻
    void on_rendezvous_datagram(const RendezvousDatagram& datagram, common::Clock::TimePoint now);

    /// REGISTER とホールパンチを送り、打ち切りの時刻を過ぎていれば失敗を知らせる。
    /// next_deadline() の時刻に呼ぶ。
    /// @param now 現在時刻
    void poll(common::Clock::TimePoint now);

    /// 親機から認証済みの応答が届いた。登録とホールパンチをやめる。
    void mark_established() noexcept;

    /// 試行をやめる。失敗は知らせない。
    void stop() noexcept;

    /// 試行中なら true を返す。
    [[nodiscard]] bool active() const noexcept;

    /// 次に poll() を呼ぶべき時刻を返す。試行中でなければ std::nullopt。
    [[nodiscard]] std::optional<common::Clock::TimePoint> next_deadline() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vocroz::network
