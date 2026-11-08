// SPDX-License-Identifier: AGPL-3.0-only
//
// 信頼制御層の 1 つのレーンの送信側。連番の付与、窓の管理、ACK の適用、再送の期限を受け持つ。
// 輻輳制御は持たない（制御メッセージは帯域をほとんど使わず、窓の上限で送りすぎを防げるため）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

#include "vocroz/common/ports/clock.h"
#include "vocroz/common/result.h"
#include "vocroz/transport/control_lane.h"
#include "vocroz/transport/transport_error.h"

#include "rtt_estimator.h"
#include "wire/control_frame_codec.h"

namespace vocroz::transport {

/// 1 つのレーンの送信側。
/// スレッド安全性: 1 つのオブジェクトは 1
/// つのスレッド（コントロールスレッド）から使う。ヒープを確保する。
class ReliableSendLane {
public:
    /// 時刻の型。
    using TimePoint = common::Clock::TimePoint;

    /// @param lane このレーン
    explicit ReliableSendLane(ControlLane lane) noexcept : lane_(lane) {}

    /// メッセージに連番を付けて送信待ちにする。送るのは append_due()。
    /// @param message メッセージ。コピーして保持する
    /// @return kPayloadTooLarge（kMaxControlMessageBytes 超）、kQueueFull、kSequenceExhausted
    [[nodiscard]] common::Result<void, TransportError> enqueue(
        std::span<const std::uint8_t> message);

    /// ACK が、送ったことのない連番を受信済みとしていないかを確かめる。状態は変えない。
    [[nodiscard]] bool is_valid_ack(const wire::AckFrame& ack) const noexcept;

    /// ACK を適用し、受信済みになったメッセージを送信待ちから外す。
    /// 前提条件: is_valid_ack(ack) が true。
    /// @param ack ACK フレーム
    /// @param now ACK を受け取った時刻
    /// @param rtt 再送していないメッセージの ACK から RTT を測って加える先
    void apply_ack(const wire::AckFrame& ack, TimePoint now, RttEstimator& rtt);

    /// 窓の中で、まだ送っていないメッセージと再送の期限が来たメッセージを writer に詰める。
    /// @param now     現在時刻
    /// @param timeout 初めて送るメッセージの再送タイムアウト
    /// @param writer  詰める先
    /// @return writer に収まらず、送るべきメッセージが残っていれば true
    [[nodiscard]] bool append_due(TimePoint now, RttEstimator::Duration timeout,
                                  wire::ControlFrameWriter& writer);

    /// 送信済みで ACK を待っているメッセージの、最も早い再送の期限を返す。なければ nullopt。
    [[nodiscard]] std::optional<TimePoint> next_retransmit_deadline() const noexcept;

    /// ACK を受け取っていないメッセージの数（未送信を含む）を返す。
    [[nodiscard]] std::size_t unacknowledged_count() const noexcept {
        return pending_.size();
    }

private:
    struct PendingMessage {
        std::uint32_t sequence;
        std::vector<std::uint8_t> message;
        TimePoint first_sent{};
        TimePoint due{};
        RttEstimator::Duration timeout{};
        bool sent = false;
        bool retransmitted = false;
        bool acknowledged = false;
    };

    // ack が sequence を受信済みとしているかを返す。
    [[nodiscard]] static bool covers(const wire::AckFrame& ack, std::uint32_t sequence) noexcept;
    // 窓の中（最も古い未 ACK のメッセージから kReliableWindowMessages 個）かを返す。
    [[nodiscard]] bool in_window(std::uint32_t sequence) const noexcept;

    ControlLane lane_;
    // 連番の昇順。先頭は常に ACK を受け取っていないメッセージ。
    std::deque<PendingMessage> pending_;
    // 次に付ける連番。uint32 の最大値は ACK で「すべて受信済み」を表せなくなるので使わない。
    std::uint32_t next_sequence_ = 0;
    // これまでに送った最大の連番 + 1。これ以上の連番を受信済みとする ACK は不正。
    std::uint32_t sent_limit_ = 0;
};

}  // namespace vocroz::transport
