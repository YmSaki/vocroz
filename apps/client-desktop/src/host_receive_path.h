// SPDX-License-Identifier: AGPL-3.0-only
//
// ネットワーク受信スレッドでの、親機のパケットの処理（docs/adr/0036-return-monitor.md）。
// network の ReceiveRouter が、ランデブーメッセージ以外のデータグラムをここへ渡す。
//
// - ハンドシェイクの応答は、そのまま PeerInbox でコントロールスレッドへ渡す（鍵交換は
//   コントロールスレッドの HostConnection が行う）
// - 鍵交換の後のパケットは受信スレッドのまま復号する。返送（下りのメディア）をコントロール
//   スレッドを経由させると、その周期（10ms）のぶん届くのが遅れて揺れ、ジッターバッファが
//   そのぶん深くなるため
// - 返送は、親機の返送ミュートの世代番号で古いフレームを捨ててから、ジッターバッファ
//   （PlayoutStream）へ入れる。制御パケットの平文と送信元の変化は、SPSC キューで
//   コントロールスレッドへ渡す
//
// 受信方向の復号器（PacketOpener）はムーブしかできず SPSC キューに載らないので、置き場の配列を
// 持ち、置き場の番号をキューで渡して所有を移す。コントロールスレッドが空いた置き場に復号器を置き、
// 番号を kAttach で渡す。受信スレッドが使い終えたら番号を返し、コントロールスレッドが片づける。
// 受信スレッドの状態はすべてこのオブジェクトの中にあり、コントロールスレッドが持つものを
// 指さないので、HostConnection が先に破棄されても受信スレッドは安全に動き続ける。
//
// コントロールスレッドからの指示は、次にデータグラムを受け取ったときに反映する。新しい接続の
// 最初のパケット（親機からの制御パケット）は、子機が接続を知らせた後にしか届かないので、
// 付け替えの指示は必ずそれより先に反映される。
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "vocroz/common/spsc_queue.h"
#include "vocroz/network/ports/datagram_listener.h"
#include "vocroz/network/udp_address.h"
#include "vocroz/transport/constants.h"
#include "vocroz/transport/playout_stream.h"
#include "vocroz/transport/secure_channel.h"
#include "vocroz/transport/send_mute_tracker.h"

#include "client_constants.h"
#include "peer_inbox.h"

namespace vocroz::client {

/// 受信スレッドからコントロールスレッドへ渡す、親機の接続の出来事 1 つ。
struct HostPacketEvent {
    enum class Kind : std::uint8_t {
        kControl,  ///< 復号した制御パケット（bytes が平文）
        kRemoteChanged,  ///< 認証済みのパケットの送信元が変わった（NAT のマッピングの変化）
    };
    Kind kind = Kind::kControl;
    /// どの接続の出来事か（HostReceivePath::attach() に渡した番号）。
    std::uint32_t generation = 0;
    /// kRemoteChanged の新しい送信元。
    network::UdpAddress from;
    /// bytes のうち有効な長さ。
    std::size_t size = 0;
    std::array<std::uint8_t, transport::kMaxPacketBytes> bytes{};

    /// 有効な範囲を返す。
    [[nodiscard]] std::span<const std::uint8_t> view() const noexcept {
        return std::span<const std::uint8_t>(bytes).first(size);
    }
};

/// コントロールスレッドから受信スレッドへの指示。
/// SPSC キューの要素は、クラスの外で定義しないと既定のコンストラクターの有無を判定できないので、
/// HostReceivePath の外に置く。
struct HostReceiveCommand {
    enum class Kind : std::uint8_t {
        kAttach,      ///< slot の復号器で受信を始める
        kDetach,      ///< generation の接続の受信を止める
        kReturnMute,  ///< generation の接続の返送ミュートの通知
    };
    Kind kind = Kind::kAttach;
    std::uint32_t generation = 0;
    std::uint8_t slot = 0;
    network::UdpAddress remote;
    bool muted = true;
    std::uint32_t epoch = 0;
};

/// 親機のパケットの受信の処理。
/// スレッド安全性: on_datagram() はネットワーク受信スレッドから、それ以外はコントロール
/// スレッドから呼ぶ（それぞれ 1 つのスレッド）。返送の PlayoutStream の render() は
/// オーディオスレッドが呼ぶ。
class HostReceivePath final : public network::DatagramListener {
public:
    /// @param handshakes ハンドシェイクの応答の渡し先。このオブジェクトより長く生存させる
    /// @param returns    返送のジッターバッファ。このオブジェクトより長く生存させる。
    ///                   enqueue() はこのクラスが受信スレッドから呼ぶ
    HostReceivePath(PeerInbox& handshakes, transport::PlayoutStream& returns) noexcept;

    // --- コントロールスレッド ---

    /// 鍵交換の済んだ接続の受信を始める。前の接続の受信は止める。
    /// 返送は、親機の返送ミュートの通知（set_return_mute()）か、新しい世代のフレームが
    /// 届くまでミュートとして扱う。
    /// @param generation 接続の番号。接続ごとに変える（出来事がどの接続のものか見分けるため）
    /// @param opener     受信方向の復号器
    /// @param remote     鍵交換の応答の送信元
    /// @return 渡せたら true。置き場か指示のキューに空きがなければ false（復号器は捨てる）
    [[nodiscard]] bool attach(std::uint32_t generation, transport::PacketOpener opener,
                              const network::UdpAddress& remote);

    /// 接続の受信を止める。generation が今の接続でなければ何もしない。
    /// @return 指示を積めたら true
    bool detach(std::uint32_t generation) noexcept;

    /// 親機からの返送ミュートの通知を、返送の受け入れの判定へ渡す。
    /// @param generation 通知が届いた接続の番号。今の接続でなければ受信スレッドが無視する
    /// @param muted      親機が返送をミュートしたか
    /// @param epoch      通知の世代番号
    /// @return 指示を積めたら true
    bool set_return_mute(std::uint32_t generation, bool muted, std::uint32_t epoch) noexcept;

    /// 出来事を 1 つ取り出す。取り出す前に、受信スレッドが手放した置き場を片づける。
    /// @return 出来事。なければ std::nullopt
    [[nodiscard]] std::optional<HostPacketEvent> pop_event();

    /// 今の接続で認証できたパケットの数（死活監視に使う）。受信スレッドが数え、単調に増える。
    [[nodiscard]] std::uint64_t authenticated_packets() const noexcept {
        return authenticated_.load(std::memory_order_acquire);
    }

    /// 捨てた出来事（キューの溢れ）の数を返す。
    [[nodiscard]] std::uint64_t dropped_events() const noexcept {
        return dropped_events_.load(std::memory_order_relaxed);
    }

    // --- ネットワーク受信スレッド ---

    /// 指示を反映してから、データグラムを処理する。ヒープを確保しない。
    void on_datagram(const network::UdpAddress& from,
                     std::span<const std::uint8_t> datagram) noexcept override;

private:
    using Command = HostReceiveCommand;

    // 受信スレッドが使っている接続。
    struct Active {
        std::uint8_t slot;
        std::uint32_t generation;
        network::UdpAddress remote;
        transport::SendMuteTracker returns;
    };

    void reclaim_slots() noexcept;
    void apply_commands() noexcept;
    void release_active() noexcept;
    void handle_packet(const network::UdpAddress& from,
                       std::span<const std::uint8_t> datagram) noexcept;
    void handle_return(std::span<const std::uint8_t> plaintext) noexcept;
    void push_event(const HostPacketEvent& event) noexcept;

    PeerInbox& handshakes_;
    transport::PlayoutStream& returns_;

    // 復号器の置き場。コントロールスレッドが置いて番号を渡すまでと、受信スレッドが番号を
    // 返した後はコントロールスレッドが、その間は受信スレッドだけが触る。
    std::array<std::optional<transport::PacketOpener>, kHostOpenerSlots> openers_;
    // コントロールスレッドだけが読み書きする、置き場の使用中の印。
    std::array<bool, kHostOpenerSlots> slot_in_use_{};

    common::SpscQueue<Command, kHostReceiveCommandCapacity> commands_;
    common::SpscQueue<std::uint8_t, kHostOpenerSlots * 2> released_;
    common::SpscQueue<HostPacketEvent, kHostPacketEventCapacity> events_;

    // --- 受信スレッドだけが読み書きする ---
    std::optional<Active> active_;
    // PlayoutStream へ渡す区間の番号。接続を付け替えたときと、返送のミュートが解除されたときに
    // 進める。親機の世代番号は接続をまたいで続くが、メディアのタイムスタンプは接続ごとに 0 から
    // 始まるので、親機の世代番号のままではジッターバッファが新しい接続のフレームを古いものとして
    // 捨ててしまう。
    std::uint32_t segment_ = 0;
    std::vector<std::uint8_t> plaintext_;

    std::atomic<std::uint64_t> authenticated_{0};
    std::atomic<std::uint64_t> dropped_events_{0};
};

}  // namespace vocroz::client
