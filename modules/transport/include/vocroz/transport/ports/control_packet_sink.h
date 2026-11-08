// SPDX-License-Identifier: AGPL-3.0-only
//
// 信頼制御層が組み立てた制御パケットの平文を送り出す先のポート。
// 平文を暗号化する PacketSealer はメディアと同じシーケンス番号を使い、1
// つのスレッドからしか使えない。
// どのスレッドで暗号化して送るかはコンポジションルートが決められるよう、信頼制御層は平文までを担う。
// 制御スレッドで暗号化してそのまま送るなら SecureControlPacketSink を使う。
#pragma once

#include <cstdint>
#include <span>

#include "vocroz/common/result.h"
#include "vocroz/transport/transport_error.h"

namespace vocroz::transport {

/// 制御パケットの平文の送り先。
class ControlPacketSink {
public:
    virtual ~ControlPacketSink() = default;

    /// 制御パケットの平文を 1
    /// つ送る。届くことは保証しなくてよい（失われたら信頼制御層が再送する）。
    /// @param plaintext 制御パケットの平文。kMaxPlaintextBytes バイト以下。空ならキープアライブ。
    ///                  呼び出しから戻った後は参照しない
    /// @return 送れなければエラー。失敗は欠落と同じに扱われ、再送で回復する
    /// スレッド安全性: 信頼制御層を使うスレッド（コントロールスレッド）から呼ばれる。
    [[nodiscard]] virtual common::Result<void, TransportError> send_control_packet(
        std::span<const std::uint8_t> plaintext) noexcept = 0;
};

}  // namespace vocroz::transport
