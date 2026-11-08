// SPDX-License-Identifier: AGPL-3.0-only
//
// 同じ UDP セッションの上で制御メッセージを確実に届ける信頼制御層。
// レーンごとの連番、選択 ACK、再送タイマーで、到達、重複の排除、送信順の受け取りを保証する。
// 輻輳制御は持たない。送信が途絶えたら 1 秒ごとにキープアライブを送る
// （docs/adr/0019-reliable-control-layer.md）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "vocroz/common/ports/clock.h"
#include "vocroz/common/result.h"
#include "vocroz/transport/control_lane.h"
#include "vocroz/transport/ports/control_message_listener.h"
#include "vocroz/transport/ports/control_packet_sink.h"
#include "vocroz/transport/transport_error.h"

namespace vocroz::transport {

/// 1 つの接続の信頼制御層。接続（ハンドシェイク）ごとに作り直す。
///
/// 使い方: 送るときは send()、制御パケットを復号したら on_control_packet()、
/// next_deadline() の時刻になったら tick() を呼ぶ。送信と ACK はその場で sink に渡す。
///
/// スレッド安全性: すべてのメンバー関数を 1 つのスレッド（コントロールスレッド）から呼ぶ。
/// ヒープを確保するので、オーディオスレッドからは呼ばない。
class ReliableControlChannel {
public:
    /// @param clock    時刻の取得元
    /// @param sink     制御パケットの平文の送り先
    /// @param listener 受け取った制御メッセージの渡し先
    /// 3 つとも、このオブジェクトより長く生存させる（AGENTS.md 4.3）。
    ReliableControlChannel(const common::Clock& clock, ControlPacketSink& sink,
                           ControlMessageListener& listener);
    ReliableControlChannel(const ReliableControlChannel&) = delete;
    ReliableControlChannel& operator=(const ReliableControlChannel&) = delete;
    ReliableControlChannel(ReliableControlChannel&&) = delete;
    ReliableControlChannel& operator=(ReliableControlChannel&&) = delete;
    ~ReliableControlChannel();

    /// メッセージを送る。窓に空きがあればその場で送り、なければ ACK を待って送る。
    /// @param lane    送るレーン。レーンの中では送った順に相手へ渡る
    /// @param message メッセージ。コピーして保持する
    /// @return 失敗の理由は kPayloadTooLarge（kMaxControlMessageBytes 超）、
    ///         kQueueFull（ACK が返らず送信待ちが上限に達した）、kSequenceExhausted
    [[nodiscard]] common::Result<void, TransportError> send(ControlLane lane,
                                                            std::span<const std::uint8_t> message);

    /// 復号した制御パケットの平文を受け取る。ACK を適用し、メッセージを listener に渡し、
    /// メッセージを受け取ったときはその場で ACK を返す。
    /// @param plaintext PacketOpener::open() が返した制御パケットの平文
    /// @return 平文の形式が不正なら kMalformedPayload で、状態は変えない
    [[nodiscard]] common::Result<void, TransportError> on_control_packet(
        std::span<const std::uint8_t> plaintext);

    /// 再送の期限が来たメッセージを再送し、送信が kKeepaliveInterval
    /// 途絶えていればキープアライブを送る。 next_deadline() より前に呼んでも害はない。
    void tick();

    /// 次に tick() を呼ぶべき時刻を返す。
    [[nodiscard]] common::Clock::TimePoint next_deadline() const noexcept;

    /// レーンの、相手の ACK をまだ受け取っていないメッセージの数（未送信を含む）を返す。
    [[nodiscard]] std::size_t unacknowledged_count(ControlLane lane) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vocroz::transport
