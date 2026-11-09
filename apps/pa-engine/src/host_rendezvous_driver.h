// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の、ランデブーサーバーへの UDP の登録の始まりと終わりを決める
// （docs/adr/0021-udp-socket-and-nat-traversal.md、docs/adr/0028-engine-assembly.md）。
// session がセッションを作ってランデブー ID を決めたら登録を始め、セッションが終わったらやめる。
// 登録の送り直しとホールパンチは network の HostRendezvous が行い、このクラスは
// ReceiveRouter から取り出したランデブーメッセージを渡し、期限に poll() するだけにする。
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "vocroz/common/ports/clock.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/network/host_rendezvous.h"
#include "vocroz/network/ports/host_rendezvous_listener.h"
#include "vocroz/network/ports/network_environment.h"
#include "vocroz/network/ports/udp_endpoint.h"
#include "vocroz/network/receive_router.h"
#include "vocroz/session/identifiers.h"
#include "vocroz/transport/ports/random_source.h"

namespace vocroz::engine {

/// 親機の UDP の登録の始まりと終わり。
/// スレッド安全性: すべてのメンバー関数をコントロールスレッドから呼ぶ。
class HostRendezvousDriver final : public network::HostRendezvousListener {
public:
    /// 注入する依存。どれもこのオブジェクトより長く生存させる（AGENTS.md 4.3）。
    struct Ports {
        network::UdpEndpoint& endpoint;  ///< 子機との通信と同じ 1 本のソケット
        network::ReceiveRouter& router;  ///< ランデブーメッセージの取り出し元
        network::NetworkEnvironment& environment;  ///< 名前解決とローカルのアドレス
        transport::RandomSource& random;           ///< ピア ID とトランザクション ID
        const common::Clock& clock;
        common::Logger& logger;
    };

    /// @param ports       注入する依存
    /// @param server_host ランデブーサーバーのホスト名。空なら登録しない（招待も作れない構成）
    /// @param server_port ランデブーサーバーの UDP のポート
    HostRendezvousDriver(Ports ports, std::string server_host, std::uint16_t server_port);

    /// 登録を始める。同じ ID で登録中なら何もしない（招待を作り直しても登録はそのまま）。
    /// 名前を解決できなければ、警告を残して登録しない（次のセッションで試し直す）。
    /// @param id session が決めたランデブー ID
    void start(const session::RendezvousId& id);

    /// 登録をやめる。セッションが終わったときに呼ぶ。
    void stop() noexcept;

    /// 受信したランデブーメッセージを渡し、期限が来ていれば登録の送り直しとホールパンチを進める。
    /// コントロールスレッドの周期ごとに呼ぶ。
    void poll();

    /// 登録とホールパンチ（HostPeerLinks が確立した経路を知らせる）。
    [[nodiscard]] network::HostRendezvous& rendezvous() noexcept {
        return rendezvous_;
    }

    /// 登録中なら true を返す。
    [[nodiscard]] bool active() const noexcept {
        return current_.has_value();
    }

    // --- network::HostRendezvousListener ---
    void on_rendezvous_available() override;
    void on_rendezvous_unavailable(network::NetworkError cause) override;

private:
    Ports ports_;
    std::string server_host_;
    std::uint16_t server_port_;
    network::HostRendezvous rendezvous_;
    std::optional<session::RendezvousId> current_;
};

}  // namespace vocroz::engine
