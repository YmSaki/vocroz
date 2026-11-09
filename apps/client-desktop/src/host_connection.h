// SPDX-License-Identifier: AGPL-3.0-only
//
// session の HostLink の実装。network（ランデブーとホールパンチ）と
// transport（Noise_IK、信頼制御層、
// 死活監視、再接続のバックオフ）をつなぎ、子機から親機への接続を確立して保つ。
//
// 手順: 招待の親機の公開鍵で Noise_IK の 1 つ目のメッセージを作り、ClientRendezvous
// が親機のすべての
// 候補へ送る。最初に認証済みの応答を返した送信元を送信先にして、送信方向の暗号化を送信スレッドへ、
// 受信方向の復号と信頼制御層をこのクラス（コントロールスレッド）に置く
// （docs/adr/0030-remote-pa-assembly.md）。
//
// ClientSession からの呼び出し（connect()、reconnect()、close()）は、次の poll() の先頭で反映する。
// 結果の通知（HostLinkListener）は poll() の最後にまとめて行う。通知の中から session が close() や
// reconnect() を呼んでも、処理中の接続を壊さないようにするため。
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "vocroz/common/ports/clock.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/network/client_rendezvous.h"
#include "vocroz/network/peer_datagram_socket.h"
#include "vocroz/network/ports/client_rendezvous_listener.h"
#include "vocroz/network/ports/network_environment.h"
#include "vocroz/network/ports/udp_endpoint.h"
#include "vocroz/network/receive_router.h"
#include "vocroz/session/ports/host_link.h"
#include "vocroz/transport/liveness_monitor.h"
#include "vocroz/transport/noise_ik_handshake.h"
#include "vocroz/transport/ports/control_message_listener.h"
#include "vocroz/transport/ports/control_packet_sink.h"
#include "vocroz/transport/ports/liveness_listener.h"
#include "vocroz/transport/ports/random_source.h"
#include "vocroz/transport/reconnect_backoff.h"
#include "vocroz/transport/reliable_control_channel.h"
#include "vocroz/transport/static_key_pair.h"

#include "host_link_listener.h"
#include "packet_sender.h"
#include "peer_inbox.h"

namespace vocroz::client {

/// 子機から親機への接続。
/// スレッド安全性: すべてのメンバー関数をコントロールスレッドから呼ぶ。
class HostConnection final : public session::HostLink,
                             public network::ClientRendezvousListener,
                             public transport::ControlMessageListener,
                             public transport::ControlPacketSink,
                             public transport::LivenessListener {
public:
    /// 注入する依存。どれもこのオブジェクトより長く生存させる（AGENTS.md 4.3）。
    struct Ports {
        network::UdpEndpoint& endpoint;  ///< ランデブーと親機との通信に使う 1 本のソケット
        network::ReceiveRouter& router;  ///< ランデブーメッセージの取り出し元
        PeerInbox& inbox;                ///< 親機のパケットの取り出し元
        PacketSender& sender;            ///< 暗号化と送信を担う送信スレッドの窓口
        network::NetworkEnvironment& environment;  ///< 名前解決とローカルのアドレス
        transport::RandomSource& random;           ///< ランデブーとバックオフの乱数
        const transport::StaticKeyPair& keys;      ///< 子機の静的鍵
        const common::Clock& clock;
        common::Logger& logger;
        HostLinkListener& listener;
    };

    /// @param ports       注入する依存
    /// @param server_host ランデブーサーバーのホスト名（UDP のランデブーに使う）
    /// @param server_port ランデブーサーバーの UDP のポート
    HostConnection(Ports ports, std::string server_host, std::uint16_t server_port);
    ~HostConnection() override;
    HostConnection(const HostConnection&) = delete;
    HostConnection& operator=(const HostConnection&) = delete;

    // --- session::HostLink ---
    void connect(const session::HostEndpoint& endpoint) override;
    void reconnect() override;
    void send(std::span<const std::uint8_t> message) override;
    void apply_return_mute(bool muted, std::uint32_t epoch) override;
    void close() override;

    /// 受信したパケットとランデブーメッセージを処理し、時刻で決まる処理（再送、死活監視、
    /// ホールパンチ、再接続の試行）を進める。コントロールスレッドの周期（数 ms〜十数
    /// ms）ごとに呼ぶ。
    void poll();

    /// 親機との接続（鍵交換の後）があれば true を返す。
    [[nodiscard]] bool linked() const noexcept {
        return link_ != nullptr;
    }

    // --- network::ClientRendezvousListener ---
    void on_path_failed(network::NetworkError cause) override;

    // --- transport::ControlMessageListener ---
    void on_control_message(transport::ControlLane lane,
                            std::span<const std::uint8_t> message) override;

    // --- transport::ControlPacketSink ---
    [[nodiscard]] common::Result<void, transport::TransportError> send_control_packet(
        std::span<const std::uint8_t> plaintext) noexcept override;

    // --- transport::LivenessListener ---
    void on_peer_timed_out() override;
    void on_peer_resumed() override;

private:
    // 接続の段階。
    enum class Phase {
        kIdle,           // 接続していない
        kConnecting,     // 最初の接続を試している
        kLinked,         // 接続している
        kReconnectWait,  // 再接続の次の試行を待っている
        kReconnecting,   // 再接続を試している
    };
    // session からの要求。次の poll() の先頭で反映する。
    enum class Request {
        kNone,
        kStart,     // 今の接続を捨てて、最初の試行を始める
        kDropLink,  // 今の接続を捨てる（再接続の待ちか、終了）
    };
    // poll() の最後に HostLinkListener へ渡す通知。
    struct Established {};
    struct Message {
        std::vector<std::uint8_t> bytes;
    };
    struct Lost {};
    struct Resumed {};
    struct Failed {
        control_protocol::ErrorCode reason;
    };
    using Event = std::variant<Established, Message, Lost, Resumed, Failed>;

    struct Link;

    void apply_request();
    void drop_link();
    void start_attempt();
    void attempt_failed();
    void handle_peer_datagram(const PeerDatagram& datagram);
    void handle_handshake_response(const PeerDatagram& datagram);
    void dispatch_events();

    Ports ports_;
    std::string server_host_;
    std::uint16_t server_port_;
    network::ClientRendezvous rendezvous_;
    transport::ReconnectBackoff backoff_;
    std::optional<session::HostEndpoint> target_;
    Phase phase_ = Phase::kIdle;
    Request request_ = Request::kNone;
    common::Clock::TimePoint retry_at_{};
    std::optional<transport::NoiseIkHandshake> handshake_;
    std::unique_ptr<Link> link_;
    std::vector<Event> events_;
};

}  // namespace vocroz::client
