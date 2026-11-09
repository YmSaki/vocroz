// SPDX-License-Identifier: AGPL-3.0-only
//
// session の PeerLink の実装。子機との接続（Noise_IK の応答側、信頼制御層、受信途絶の判定）を
// コントロールスレッドで持ち、受信の処理（PeerReceivePath）とミキサー（EngineMixer）をつなぐ
// （docs/adr/0028-engine-assembly.md）。
//
// 手順: 子機の鍵交換の 1 つ目のメッセージに応答し、接続ごとに受信方向の復号器を受信スレッドへ渡す。
// session が承認したら、ストリームごとにジッターバッファを作ってストリップにつなぎ、受信スレッドに
// メディアの受け付けを指示してから on_peer_established() を知らせる。承認のあとも、再接続と
// 入力の申告の変更のたびに admit() で対応が置き換わる（docs/adr/0035-client-input-expansion.md）。
//
// 子機は応答が届くまで同じ 1 つ目のメッセージを送り直し、複数の経路から同じものが届くこともある。
// 接続 ID が同じものは重複として、保存した応答を送り直すだけにする（接続を作り直さない）。
//
// 全体の目標レイテンシー（PlayoutControl）もここで持つ。ジッターバッファを作るのがここなので、
// 今あるストリームに配り、あとから作るストリームも同じ目標で作る（docs/adr/0037-latency-target-and-link-stats.md）。
//
// session への通知（PeerEventSink）は、処理の最後にまとめて行う。通知の中から session が
// PeerLink を呼んでも、処理中の接続を壊さないようにするため。
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "vocroz/common/ports/clock.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/network/host_rendezvous.h"
#include "vocroz/network/ports/udp_endpoint.h"
#include "vocroz/network/udp_address.h"
#include "vocroz/session/ports/peer_link.h"
#include "vocroz/session/ports/playout_control.h"
#include "vocroz/transport/playout_stream.h"
#include "vocroz/transport/static_key_pair.h"

#include "engine_mixer.h"
#include "peer_event_sink.h"
#include "peer_receive_listener.h"
#include "peer_receive_path.h"

namespace vocroz::engine {

/// 親機側の、子機との接続の集まり。
/// スレッド安全性: すべてのメンバー関数をコントロールスレッドから呼ぶ。
/// 破棄する前に、受信スレッド（UdpEndpoint）を止める。
class HostPeerLinks final : public session::PeerLink,
                            public session::PlayoutControl,
                            public PeerReceiveListener {
public:
    /// 注入する依存。どれもこのオブジェクトより長く生存させる（AGENTS.md 4.3）。
    struct Ports {
        network::UdpEndpoint& endpoint;  ///< 子機への送信と、受信スレッドを起こす送信
        PeerReceivePath& receive;        ///< 受信スレッドへの指示の渡し先
        EngineMixer& mixer;              ///< ジッターバッファをつなぐストリップ
        network::HostRendezvous& rendezvous;  ///< 確立した経路のホールパンチを止める
        PeerEventSink& events;                ///< session への通知
        const transport::StaticKeyPair& keys;  ///< 親機の静的鍵
        const common::Clock& clock;
        common::Logger& logger;
    };

    /// @param ports   注入する依存
    /// @param playout 接続ごとのジッターバッファの設定
    HostPeerLinks(Ports ports, const transport::PlayoutConfig& playout);
    ~HostPeerLinks() override;
    HostPeerLinks(const HostPeerLinks&) = delete;
    HostPeerLinks& operator=(const HostPeerLinks&) = delete;

    /// 時刻で決まる処理（信頼制御層の再送とキープアライブ、受信途絶の判定）を進め、
    /// 積めなかった指示を積み直し、受信スレッドを起こす。コントロールスレッドの周期ごとに呼ぶ。
    void poll();

    /// 今ある接続（閉じている途中のものを除く）の数を返す。
    [[nodiscard]] std::size_t peer_count() const noexcept;

    // --- session::PeerLink ---
    void send(session::PeerHandle peer, std::span<const std::uint8_t> message) override;
    void admit(session::PeerHandle peer, const std::vector<session::StreamRoute>& routes) override;
    void apply_send_mute(session::PeerHandle peer, bool send_muted, std::uint32_t epoch) override;
    void set_return_mute(session::PeerHandle peer, bool muted, std::uint32_t epoch) override;
    void close(session::PeerHandle peer) override;

    // --- session::PlayoutControl ---
    void set_latency_target(std::chrono::microseconds target) override;

    // --- PeerReceiveListener（PeerReceiveRelay がコントロールスレッドで呼ぶ） ---
    void on_handshake_initiation(const network::UdpAddress& from,
                                 std::span<const std::uint8_t> datagram) override;
    void on_control_packet(session::PeerHandle peer,
                           std::span<const std::uint8_t> plaintext) override;
    void on_signal_state(session::PeerHandle peer, transport::SignalState state) override;
    void on_receive_released(session::PeerHandle peer) override;

private:
    struct Peer;

    // 閉じている途中のものを除いて探す。
    [[nodiscard]] Peer* find(session::PeerHandle handle);
    [[nodiscard]] Peer* find_by_connection(std::uint32_t connection_id);
    void accept(const network::UdpAddress& from, std::span<const std::uint8_t> datagram);
    // 対応に合わせてストリームとストリップの接続を置き換える。すべてつなげたら true。
    [[nodiscard]] bool apply_routes(Peer& peer, const std::vector<session::StreamRoute>& routes);
    void detach_strip(Peer& peer, const std::string& strip_id);
    void begin_close(Peer& peer);
    void command(const ReceiveCommand& command);
    void flush_commands();
    void defer(std::function<void()> action);
    void dispatch_events();

    Ports ports_;
    transport::PlayoutConfig playout_;
    // 受信スレッドを起こすデータグラムの宛先（自分のポートのループバック）。
    network::UdpAddress wake_address_;
    common::Clock::TimePoint next_wake_{};
    session::PeerHandle next_handle_ = 1;
    std::map<session::PeerHandle, std::unique_ptr<Peer>> peers_;
    // ストリップにつないでいる接続。張り直しでは新しい接続が先につながるので、閉じるときは
    // 自分がつないでいるときだけ外す。
    std::map<std::string, session::PeerHandle, std::less<>> strip_owners_;
    // キューが満杯で積めなかった指示。順序を保って次の poll() で積み直す。
    std::deque<ReceiveCommand> pending_commands_;
    std::vector<std::function<void()>> events_;
};

}  // namespace vocroz::engine
