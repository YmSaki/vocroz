// SPDX-License-Identifier: AGPL-3.0-only
//
// 制御パケットの平文を PacketSealer で暗号化し、DatagramSocket で送る ControlPacketSink。
// 送信方向の PacketSealer をコントロールスレッドだけが使う構成（メディアを送らない方向）で使う。
// メディアと同じ方向で sealer を共有するなら、sealer を持つスレッドへ平文を渡す実装を別に用意する。
#pragma once

#include <cstdint>
#include <span>

#include "vocroz/common/result.h"
#include "vocroz/transport/ports/control_packet_sink.h"
#include "vocroz/transport/ports/datagram_socket.h"
#include "vocroz/transport/secure_channel.h"
#include "vocroz/transport/transport_error.h"

namespace vocroz::transport {

/// 暗号化して送る ControlPacketSink。
/// スレッド安全性: sealer を使うスレッドから呼ぶ。ヒープを確保せず、ブロックしない。
class SecureControlPacketSink final : public ControlPacketSink {
public:
    /// @param sealer 送信方向の暗号化。このオブジェクトより長く生存させる
    /// @param socket 送り先。このオブジェクトより長く生存させる
    SecureControlPacketSink(PacketSealer& sealer, DatagramSocket& socket) noexcept
        : sealer_(sealer), socket_(socket) {}

    /// 平文を暗号化して送る。
    /// @return 暗号化の失敗（PacketSealer::seal_control() のエラー）か、kSendFailed
    [[nodiscard]] common::Result<void, TransportError> send_control_packet(
        std::span<const std::uint8_t> plaintext) noexcept override;

private:
    PacketSealer& sealer_;
    DatagramSocket& socket_;
};

}  // namespace vocroz::transport
