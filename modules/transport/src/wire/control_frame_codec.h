// SPDX-License-Identifier: AGPL-3.0-only
//
// 制御パケットの平文（ACK とデータのフレームの並び）の読み書き。
// 受信した平文はここで検証し、内側には検証済みのフレームだけを渡す（AGENTS.md 6.3）。
// 形式は docs/adr/0019-reliable-control-layer.md にある。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "vocroz/common/result.h"
#include "vocroz/transport/control_lane.h"
#include "vocroz/transport/secure_channel.h"
#include "vocroz/transport/transport_error.h"

namespace vocroz::transport::wire {

/// ACK フレーム。受信側が、レーンごとの受信状況を送信側に伝える。
struct AckFrame {
    /// 対象のレーン。
    ControlLane lane;
    /// 次に受け取りたい連番。これより小さい連番はすべて受信済み。
    std::uint32_t cumulative;
    /// ビット i が 1 なら、連番 cumulative + 1 + i を受信済み（選択 ACK）。
    std::uint64_t selective;
};

/// データフレーム。制御メッセージ 1 つを運ぶ。
struct DataFrame {
    /// 対象のレーン。
    ControlLane lane;
    /// レーンごとの連番。0 から 1 ずつ進む。
    std::uint32_t sequence;
    /// メッセージの中身。解析した平文の一部を指す。
    std::span<const std::uint8_t> message;
};

/// 解析した制御パケット。
struct ControlFrames {
    /// ACK フレーム（平文に現れた順）。
    std::vector<AckFrame> acks;
    /// データフレーム（平文に現れた順）。
    std::vector<DataFrame> data;
};

/// 制御パケットの平文を解析する。空の平文はキープアライブで、フレームを持たない。
/// 1 つでも不正なフレームがあれば、パケット全体を不正として何も返さない
/// （一部だけを適用して、状態が中途半端に変わるのを防ぐため）。
/// @param plaintext 認証に成功した制御パケットの平文
/// @return 解析したフレーム。未知のフレーム種別、未知のレーン、途中で切れたフレームは
/// kMalformedPayload スレッド安全性: 純粋関数で、どのスレッドからも呼び出し可能。ヒープを確保する。
[[nodiscard]] common::Result<ControlFrames, TransportError> parse_control_frames(
    std::span<const std::uint8_t> plaintext);

/// 制御パケットの平文を固定長のバッファに組み立てる。
/// スレッド安全性: 1 つのオブジェクトは 1 つのスレッドから使う。ヒープを確保しない。
class ControlFrameWriter {
public:
    /// ACK フレームを追加する。
    /// @return 残りの長さが足りなければ false で、何も書かない
    [[nodiscard]] bool append_ack(const AckFrame& frame) noexcept;

    /// データフレームを追加する。
    /// @return 残りの長さが足りなければ false で、何も書かない
    [[nodiscard]] bool append_data(const DataFrame& frame) noexcept;

    /// 組み立てた平文を返す。
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept {
        return std::span(buffer_).first(size_);
    }

    /// フレームを 1 つも書いていなければ true を返す。
    [[nodiscard]] bool empty() const noexcept {
        return size_ == 0;
    }

    /// 書いたフレームを捨て、空に戻す。
    void clear() noexcept {
        size_ = 0;
    }

private:
    std::array<std::uint8_t, kMaxPlaintextBytes> buffer_{};
    std::size_t size_ = 0;
};

}  // namespace vocroz::transport::wire
