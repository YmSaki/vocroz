// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の、ランデブーサーバーへの登録と子機へのホールパンチ。
// セッションの間、10 秒ごとに REGISTER を送り直し、PUNCH を受け取るたびに子機のすべての候補へ
// 空のデータグラムを送って、親機の側の NAT
// のマッピングを開く（docs/adr/0021-udp-socket-and-nat-traversal.md）。
// 子機が送る鍵交換のメッセージがこのマッピングを通って届く。
#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "vocroz/common/ports/clock.h"
#include "vocroz/network/ports/host_rendezvous_listener.h"
#include "vocroz/network/ports/udp_endpoint.h"
#include "vocroz/network/receive_router.h"
#include "vocroz/network/rendezvous_types.h"
#include "vocroz/network/udp_address.h"
#include "vocroz/transport/ports/random_source.h"

namespace vocroz::network {

/// 親機の登録の要求。
struct HostRendezvousRequest {
    /// ランデブーサーバーのアドレス（select_server_address() で選ぶ）。
    UdpAddress server;
    /// セッションの作成時に session が乱数で決めたランデブー ID。
    RendezvousId rendezvous_id{};
    /// 申告するローカル候補（select_local_candidates()
    /// で選ぶ。ポート開放の外部アドレスを先頭に含める）。
    std::vector<UdpAddress> local_candidates;
};

/// 親機の登録とホールパンチ。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッド（コントロールスレッド）から使う。
class HostRendezvous {
public:
    /// @param endpoint 送信に使うソケット。HostRendezvous より長く生きること
    /// @param random   ピア ID とトランザクション ID に使う乱数。HostRendezvous より長く生きること
    /// @param listener 登録の状態の受け取り手。HostRendezvous より長く生きること
    HostRendezvous(UdpEndpoint& endpoint, transport::RandomSource& random,
                   HostRendezvousListener& listener);
    ~HostRendezvous();
    HostRendezvous(const HostRendezvous&) = delete;
    HostRendezvous& operator=(const HostRendezvous&) = delete;

    /// 登録を始める。登録中なら最初からやり直す（ピア ID も新しくする）。
    /// @param request 要求
    /// @param now     現在時刻
    void start(const HostRendezvousRequest& request, common::Clock::TimePoint now);

    /// 申告する候補を置き換える（ポート開放の更新で外部アドレスが変わったときなど）。次の REGISTER
    /// から反映する。
    /// @param candidates 新しい候補
    void set_local_candidates(std::vector<UdpAddress> candidates);

    /// ReceiveRouter から取り出したランデブーメッセージを渡す。
    /// @param datagram メッセージ
    /// @param now      現在時刻
    void on_rendezvous_datagram(const RendezvousDatagram& datagram, common::Clock::TimePoint now);

    /// REGISTER とホールパンチを送り、登録の状態の変化を知らせる。next_deadline() の時刻に呼ぶ。
    /// @param now 現在時刻
    void poll(common::Clock::TimePoint now);

    /// 子機から認証済みのパケットが届いた。その送信元を候補に含む子機へのホールパンチをやめる。
    /// 一致する子機がなくても（子機の NAT が送信先ごとにポートを変える場合など）、
    /// ホールパンチは送り続ける時間を過ぎれば自然に終わる。
    /// @param peer_address 認証済みのパケットの送信元
    void mark_established(const UdpAddress& peer_address);

    /// 登録とホールパンチをやめる。セッションの終了時に呼ぶ。
    void stop() noexcept;

    /// 次に poll() を呼ぶべき時刻を返す。登録中でなければ std::nullopt。
    [[nodiscard]] std::optional<common::Clock::TimePoint> next_deadline() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vocroz::network
