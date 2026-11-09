// SPDX-License-Identifier: AGPL-3.0-only
//
// ランデブーサーバーへの登録（REGISTER の送信と送り直し）と、REGISTERED と PUNCH の受け付け。
// 登録はソフトステートなので、決まった間隔で送り直す（docs/adr/0012-rendezvous-protocol.md）。
// 届いたメッセージは、ランデブーサーバーから届き、自分の登録に対応するものだけを受け付ける。
// 第三者が偽の PUNCH で、関係のない宛先へホールパンチを送らせることを防ぐため。
#pragma once

#include <chrono>
#include <optional>
#include <vector>

#include "vocroz/common/ports/clock.h"
#include "vocroz/network/ports/udp_endpoint.h"
#include "vocroz/network/receive_router.h"
#include "vocroz/network/rendezvous_types.h"
#include "vocroz/network/udp_address.h"

#include "wire/rendezvous_codec.h"

namespace vocroz::network {

/// 登録の内容。
struct RegistrationConfig {
    /// ランデブーサーバーのアドレス。
    UdpAddress server;
    RendezvousId rendezvous_id{};
    PeerId peer_id{};
    RendezvousRole role = RendezvousRole::kClient;
    /// 申告するローカル候補。kMaxLocalCandidates 個以下。
    std::vector<UdpAddress> candidates;
    /// 登録できた後に送り直す間隔。登録できるまでは kRegisterRetryInterval で送る。
    std::chrono::milliseconds refresh_interval{0};
};

/// 1 つの登録。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッド（コントロールスレッド）から使う。
class RendezvousRegistration {
public:
    /// 受け付けたメッセージの受け取り手。
    class Listener {
    public:
        virtual ~Listener() = default;
        /// 自分の REGISTER への REGISTERED を受け取った。
        virtual void on_registered(const wire::RegisteredMessage& message) = 0;
        /// 自分の登録への PUNCH を受け取った。相手の役割が自分と違うことは確かめてある。
        virtual void on_punch(const wire::PunchMessage& message) = 0;
    };

    /// @param endpoint REGISTER を送るソケット。RendezvousRegistration より長く生きること
    /// @param listener 受け付けたメッセージの受け取り手。RendezvousRegistration より長く生きること
    RendezvousRegistration(UdpEndpoint& endpoint, Listener& listener) noexcept;

    /// 登録を始め、すぐに REGISTER を送る。登録中なら内容を置き換える。
    /// @param config 登録の内容
    /// @param tx_id  この登録のトランザクション ID（乱数）
    /// @param now    現在時刻
    void start(RegistrationConfig config, const wire::TxId& tx_id, common::Clock::TimePoint now);

    /// 登録をやめる。REGISTER を送らなくなり、届いたメッセージも受け付けない。
    /// サーバーの登録は寿命（30 秒）で消える。
    void stop() noexcept;

    /// 次に申告する候補を置き換える。次の REGISTER から反映する。
    /// @param candidates 新しい候補。kMaxLocalCandidates 個以下
    void set_candidates(std::vector<UdpAddress> candidates);

    /// 送り直しの時刻なら REGISTER を送る。
    /// @param now 現在時刻
    void poll(common::Clock::TimePoint now);

    /// ランデブーメッセージを受け付ける。サーバー以外から届いたもの、自分の登録に対応しないものは捨てる。
    /// @param datagram ReceiveRouter から取り出したメッセージ
    /// @param now      現在時刻
    void on_datagram(const RendezvousDatagram& datagram, common::Clock::TimePoint now);

    /// 登録中なら true を返す。
    [[nodiscard]] bool active() const noexcept {
        return config_.has_value();
    }

    /// 登録を始めた時刻を返す。登録中でなければ意味のない値。
    [[nodiscard]] common::Clock::TimePoint started_at() const noexcept {
        return started_at_;
    }

    /// 最後に REGISTERED を受け取った時刻を返す。まだなら std::nullopt。
    [[nodiscard]] std::optional<common::Clock::TimePoint> last_registered_at() const noexcept {
        return last_registered_at_;
    }

    /// 次に poll() を呼ぶべき時刻を返す。登録中でなければ std::nullopt。
    [[nodiscard]] std::optional<common::Clock::TimePoint> next_deadline() const noexcept;

private:
    void send_register();

    UdpEndpoint& endpoint_;
    Listener& listener_;
    std::optional<RegistrationConfig> config_;
    wire::TxId tx_id_{};
    common::Clock::TimePoint started_at_{};
    common::Clock::TimePoint next_send_at_{};
    std::optional<common::Clock::TimePoint> last_registered_at_;
};

}  // namespace vocroz::network
