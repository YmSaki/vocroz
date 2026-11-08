// SPDX-License-Identifier: AGPL-3.0-only
//
// 信頼制御層の 1 つのレーンの受信側。重複の排除、順序の整列、ACK の内容を受け持つ。
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "vocroz/transport/control_lane.h"
#include "vocroz/transport/ports/control_message_listener.h"

#include "transport_constants.h"
#include "wire/control_frame_codec.h"

namespace vocroz::transport {

/// 1 つのレーンの受信側。
/// スレッド安全性: 1 つのオブジェクトは 1
/// つのスレッド（コントロールスレッド）から使う。ヒープを確保する。
class ReliableReceiveLane {
public:
    /// @param lane このレーン
    explicit ReliableReceiveLane(ControlLane lane) noexcept : lane_(lane) {}

    /// データフレームを受け取り、連番の順にそろったメッセージを listener に渡す。
    /// 受信済みの連番は捨て、窓より先の連番も捨てる（送信側は窓を超えて送らないので、届くのは改変か不具合）。
    /// どの場合も ACK を返す必要があるとして記録する（前の ACK
    /// が失われていれば、送信側は再送を続けるため）。
    /// @param frame    データフレーム
    /// @param listener 順番がそろったメッセージの渡し先
    void accept(const wire::DataFrame& frame, ControlMessageListener& listener);

    /// 現在の受信状況を表す ACK フレームを返す。
    [[nodiscard]] wire::AckFrame ack() const noexcept;

    /// 前回 ACK を送ってからデータフレームを受け取っていれば true を返す。
    [[nodiscard]] bool ack_pending() const noexcept {
        return ack_pending_;
    }

    /// ACK を送ったことを記録する。
    void mark_ack_sent() noexcept {
        ack_pending_ = false;
    }

private:
    // 連番 sequence を置く場所。窓の幅で循環させる。
    [[nodiscard]] static std::size_t slot_of(std::uint64_t sequence) noexcept {
        return static_cast<std::size_t>(sequence % kReliableWindowMessages);
    }

    ControlLane lane_;
    // 次に渡す連番。ACK の累積 ACK になる。送信側は uint32 の最大値を使わないので、uint32
    // に収まる。
    std::uint32_t next_expected_ = 0;
    // next_expected_ より後に届いた、まだ渡せないメッセージ。
    std::array<std::optional<std::vector<std::uint8_t>>, kReliableWindowMessages> slots_{};
    bool ack_pending_ = false;
};

}  // namespace vocroz::transport
