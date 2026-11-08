// SPDX-License-Identifier: AGPL-3.0-only
//
// 1 つの相手に UDP のデータグラムを送るソケットのポート。実装は network が持つ（Winsock）。
// 受信はソケットの実装がネットワーク受信スレッドで待ち、届いたバイト列を PacketOpener に渡す。
// transport は受信の待ち方を決めないので、このポートは送信だけを持つ。
#pragma once

#include <cstdint>
#include <span>

#include "vocroz/common/result.h"
#include "vocroz/transport/transport_error.h"

namespace vocroz::transport {

/// 接続先が決まっている UDP ソケットの送信側。
/// 接続先のアドレスは network が経路の確立（直結かホールパンチ）のときに決め、transport
/// は扱わない。
class DatagramSocket {
public:
    virtual ~DatagramSocket() = default;

    /// データグラムを 1 つ送る。ブロックしない。届くことは保証しない。
    /// @param datagram UDP のペイロード。kMaxPacketBytes
    /// バイト以下。呼び出しから戻った後は参照しない
    /// @return 送れなければ kSendFailed
    /// スレッド安全性: 実装ごとに異なる。1
    /// つのオブジェクトを複数のスレッドから使うときは実装の説明に従う。
    [[nodiscard]] virtual common::Result<void, TransportError> send(
        std::span<const std::uint8_t> datagram) noexcept = 0;
};

}  // namespace vocroz::transport
