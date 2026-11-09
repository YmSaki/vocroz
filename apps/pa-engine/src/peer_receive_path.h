// SPDX-License-Identifier: AGPL-3.0-only
//
// ネットワーク受信スレッドでの、子機のパケットの処理（docs/adr/0028-engine-assembly.md）。
// network の ReceiveRouter が、ランデブーメッセージ以外のデータグラムをここへ渡す。
//
// - ヘッダーの接続 ID で接続を選び、復号する（送信元のアドレスではなく接続 ID で選ぶので、
//   NAT のマッピングが変わっても同じ接続として扱え、送信先を新しい送信元へ移す）
// - メディアは承認済みの接続のものだけを、信号状態の判定（古い世代の選別）を通して、
//   ストリーム ID
//   ごとのジッターバッファ（PlayoutStream）へ受信スレッドのまま入れる。コントロールスレッドを
//   経由させると、その周期のぶん音が遅れるため
// - 鍵交換の 1 つ目のメッセージと制御パケットは PeerReceiveListener でコントロールスレッドへ渡す
//
// 接続の追加、承認、送信ミュート、取り外しは、コントロールスレッドが SPSC キューで指示し、
// 次にデータグラムを受け取ったときに反映する。受信スレッドはデータグラムが届くまで待つので、
// コントロールスレッドは定期的に自分のポートへ短いデータグラムを送って起こす
// （HostPeerLinks::poll()）。起こされたときにも信号状態のタイムアウトを判定する。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "vocroz/common/ports/clock.h"
#include "vocroz/common/spsc_queue.h"
#include "vocroz/control_protocol/constants.h"
#include "vocroz/network/peer_datagram_socket.h"
#include "vocroz/network/ports/datagram_listener.h"
#include "vocroz/network/udp_address.h"
#include "vocroz/session/identifiers.h"
#include "vocroz/transport/liveness_monitor.h"
#include "vocroz/transport/playout_stream.h"
#include "vocroz/transport/secure_channel.h"
#include "vocroz/transport/signal_monitor.h"

#include "engine_constants.h"
#include "peer_receive_listener.h"

namespace vocroz::engine {

/// 1 つの接続の、ストリーム ID ごとの流し込み先（ジッターバッファ）の表。
/// コントロールスレッドが承認と申告の変更のたびに新しく作り、kAdmit の指示で受信スレッドに渡す。
/// 渡したあとは変更しない（読むのは受信スレッドだけ）。接続を破棄するまで保つ。
struct StreamTable {
    struct Slot {
        std::uint16_t stream_id = 0;
        transport::PlayoutStream* playout = nullptr;
    };
    std::array<Slot, control_protocol::kMaxInputStreams> slots{};
    std::size_t count = 0;

    /// ストリーム ID の流し込み先を返す。なければ null。
    [[nodiscard]] transport::PlayoutStream* find(std::uint16_t stream_id) const noexcept;
};

/// 1 つの接続の、受信側の状態。
/// コントロールスレッドが作り、kAttach の指示で受信スレッドに渡す。渡したあとは
/// 受信スレッドだけが読み書きする。コントロールスレッドは、on_receive_released() の
/// 通知のあとに破棄する。
/// 参照先（liveness、socket、streams の表とジッターバッファ）は、破棄されるまでコントロール
/// スレッドが保つ。
struct ReceivePeer {
    /// @param handle_in   session に見せる接続の識別子
    /// @param opener_in   鍵交換で得た受信方向の復号器
    /// @param remote_in   鍵交換の 1 つ目のメッセージの送信元
    /// @param liveness_in 受信途絶の判定（記録は受信スレッド、判定はコントロールスレッド）
    /// @param socket_in   この接続への送信に使うソケット（送信先の更新だけを受信スレッドが行う）
    /// @param now         現在時刻（信号状態の判定の起点）
    ReceivePeer(session::PeerHandle handle_in, transport::PacketOpener opener_in,
                const network::UdpAddress& remote_in, transport::LivenessMonitor& liveness_in,
                network::PeerDatagramSocket& socket_in, common::Clock::TimePoint now);

    session::PeerHandle handle;
    transport::PacketOpener opener;
    network::UdpAddress remote;
    transport::LivenessMonitor& liveness;
    network::PeerDatagramSocket& socket;
    // 承認で渡されるストリームの表。承認までは null で、メディアをジッターバッファへ入れない
    // （承認前遮断）。表にないストリームのメディアは捨てる。enqueue() は受信スレッドだけが呼ぶ。
    const StreamTable* streams = nullptr;
    transport::SignalMonitor signal;
    // コントロールスレッドへ最後に知らせた信号状態。子機は送信ミュートで参加するので PAUSED から。
    transport::SignalState reported = transport::SignalState::kPaused;
    std::vector<std::uint8_t> plaintext;
};

/// コントロールスレッドから受信スレッドへの指示。
struct ReceiveCommand {
    enum class Kind : std::uint8_t {
        kAttach,  ///< 接続を受信の対象に加える
        kAdmit,  ///< streams の表でメディアをジッターバッファへ入れ始める（表を置き換える）
        kSendMute,  ///< 子機の送信ミュートの通知を信号状態の判定へ渡す
        kDetach,    ///< 受信の対象から外し、on_receive_released() で知らせる
    };
    Kind kind = Kind::kAttach;
    ReceivePeer* peer = nullptr;
    const StreamTable* streams = nullptr;
    bool send_muted = true;
    std::uint32_t mute_epoch = 0;
};

/// 子機のパケットの受信の処理。
/// スレッド安全性: on_datagram() はネットワーク受信スレッドから、push() はコントロールスレッド
/// から呼ぶ（それぞれ 1 つのスレッド）。
class PeerReceivePath final : public network::DatagramListener {
public:
    /// @param listener 通知の渡し先。このオブジェクトより長く生存させる
    /// @param clock    信号状態の判定に使う時刻。このオブジェクトより長く生存させる
    PeerReceivePath(PeerReceiveListener& listener, const common::Clock& clock) noexcept;

    /// 指示を積む。
    /// @param command 指示
    /// @return 積めたら true。キューが満杯なら false（呼び出し側が後で積み直す）
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    [[nodiscard]] bool push(const ReceiveCommand& command) noexcept {
        return commands_.try_push(command);
    }

    /// 指示を反映し、信号状態のタイムアウトを判定してから、データグラムを処理する。
    /// ヘッダーを読めないもの（受信スレッドを起こすためのデータグラムを含む）は、それ以上は扱わない。
    /// スレッド安全性: ネットワーク受信スレッドから呼ぶ。
    void on_datagram(const network::UdpAddress& from,
                     std::span<const std::uint8_t> datagram) noexcept override;

private:
    void apply_commands() noexcept;
    void poll_signals(common::Clock::TimePoint now) noexcept;
    void handle_packet(ReceivePeer& peer, const network::UdpAddress& from,
                       std::span<const std::uint8_t> datagram,
                       common::Clock::TimePoint now) noexcept;
    void handle_media(ReceivePeer& peer, std::span<const std::uint8_t> plaintext,
                      common::Clock::TimePoint now) noexcept;
    void report_signal(ReceivePeer& peer) noexcept;
    [[nodiscard]] ReceivePeer* find(std::uint32_t connection_id) noexcept;

    PeerReceiveListener& listener_;
    const common::Clock& clock_;
    common::SpscQueue<ReceiveCommand, kReceiveCommandCapacity> commands_;
    // 受信の対象の接続。空きは null。接続 ID の検索は数十件の線形探索で足りる。
    std::array<ReceivePeer*, kMaxPeerConnections> peers_{};
};

}  // namespace vocroz::engine
