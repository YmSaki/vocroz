// SPDX-License-Identifier: AGPL-3.0-only
//
// 1 つの接続の送信方向。制御パケットの平文（コントロールスレッドの信頼制御層から）と、
// エンコード済みのメディアフレーム（オーディオスレッドから）を SPSC キューで受け取り、
// 送信スレッドの pump() で暗号化して送る。
//
// 送信方向の PacketSealer は 1 つのスレッドからしか使えず（メディアと制御で同じシーケンス番号を
// 使うため）、ソケットへの送信はオーディオスレッドでできないので、送信スレッドが両方を暗号化して
// 送る。子機の送信（client-desktop の PacketSender）と同じ規則で、親機は接続ごとにこれを持つ
// （docs/adr/0036-return-monitor.md）。
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "vocroz/audio_core/realtime.h"
#include "vocroz/common/spsc_queue.h"
#include "vocroz/transport/constants.h"
#include "vocroz/transport/media_frame.h"
#include "vocroz/transport/ports/control_packet_sink.h"
#include "vocroz/transport/ports/datagram_socket.h"
#include "vocroz/transport/ports/media_frame_sink.h"
#include "vocroz/transport/ports/send_notifier.h"
#include "vocroz/transport/secure_channel.h"

namespace vocroz::transport {

/// 送信の統計。
struct PeerPacketSenderStats {
    std::uint64_t media_sent = 0;     ///< 送ったメディアパケット
    std::uint64_t control_sent = 0;   ///< 送った制御パケット
    std::uint64_t dropped = 0;        ///< 送信待ちが満杯で入らなかったパケット
    std::uint64_t send_failures = 0;  ///< 暗号化か送信に失敗したパケット
};

/// 送信待ちの制御パケットの平文 1 つ。SPSC キューに入れるので固定長にする。
struct QueuedControlPlaintext {
    std::size_t size = 0;                                  ///< bytes のうち有効な長さ
    std::array<std::uint8_t, kMaxPlaintextBytes> bytes{};  ///< 平文
};

/// 1 つの接続の、暗号化と送信。
/// 生産者は 2 つ（コントロールスレッドの send_control_packet()、オーディオスレッドの
/// push_frame()）、消費者は送信スレッドの pump() だけ。
class PeerPacketSender final : public ControlPacketSink, public MediaFrameSink {
public:
    /// オーディオスレッドから送信スレッドへ渡す、エンコード済みフレームの数の上限。
    /// 5ms フレームで 160ms 分。送信スレッドがこれより長く止まったら新しいフレームを捨てる
    /// （受信側からは欠損と同じに見える）。
    static constexpr std::size_t kFrameQueueCapacity = 32;
    /// コントロールスレッドから送信スレッドへ渡す、制御パケットの平文の数の上限。
    /// 信頼制御層の窓より十分大きくし、通常の操作では溢れないようにする。溢れた平文は
    /// 失われたものとして信頼制御層が再送する。
    static constexpr std::size_t kControlQueueCapacity = 64;

    /// @param sealer          送信方向の暗号化。所有権ごと受け取る
    /// @param socket          送信先。pump() を呼ぶ間は生存させる
    /// @param media_stream_id メディアパケットに載せるストリーム ID
    /// @param notifier        送るものを入れたときの通知先。このオブジェクトより長く生存させる
    /// スレッド安全性: コントロールスレッドで生成する（送信用のバッファを確保する）。
    PeerPacketSender(PacketSealer sealer, DatagramSocket& socket, std::uint16_t media_stream_id,
                     SendNotifier& notifier);
    PeerPacketSender(const PeerPacketSender&) = delete;
    PeerPacketSender& operator=(const PeerPacketSender&) = delete;

    /// 制御パケットの平文を送信待ちに入れる（ControlPacketSink）。
    /// @return 入れられなければ kPayloadTooLarge（長すぎる）か kSendFailed（満杯）。
    ///         どちらも欠落と同じに扱われ、信頼制御層が再送する
    /// スレッド安全性: コントロールスレッド（生産者は 1 つ）から呼ぶ。
    [[nodiscard]] common::Result<void, TransportError> send_control_packet(
        std::span<const std::uint8_t> plaintext) noexcept override;

    /// エンコード済みのフレームを送信待ちに入れる（MediaFrameSink）。
    /// @return 入れられたら true。満杯なら false
    /// スレッド安全性: オーディオスレッド（生産者は 1 つ）から呼び出し可能。
    [[nodiscard]] bool push_frame(const MediaFrame& frame) noexcept VOCROZ_NONBLOCKING override;

    /// 送信待ちのパケットをすべて暗号化して送る。制御パケットを先に送る。
    /// スレッド安全性: 送信スレッド（消費者は 1 つ）から呼ぶ。ソケットへの送信でブロックしうる。
    void pump() noexcept;

    /// 統計を返す。
    /// スレッド安全性: どのスレッドからも呼び出し可能（値はおおよそ）。
    [[nodiscard]] PeerPacketSenderStats stats() const noexcept;

private:
    void send_sealed(std::size_t bytes, std::atomic<std::uint64_t>& sent_counter) noexcept;

    PacketSealer sealer_;
    DatagramSocket& socket_;
    std::uint16_t media_stream_id_;
    SendNotifier& notifier_;
    common::SpscQueue<MediaFrame, kFrameQueueCapacity> frames_;
    common::SpscQueue<QueuedControlPlaintext, kControlQueueCapacity> control_;
    // 送信スレッドだけが使う。
    std::vector<std::uint8_t> packet_;
    std::atomic<std::uint64_t> media_sent_{0};
    std::atomic<std::uint64_t> control_sent_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint64_t> send_failures_{0};
};

}  // namespace vocroz::transport
