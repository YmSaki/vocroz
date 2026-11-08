// SPDX-License-Identifier: AGPL-3.0-only
//
// UDP ソケットが受信したデータグラムを受け取るリスナー。
// ソケットの実装（Winsock）がネットワーク受信スレッドで呼ぶ。届いたデータグラムを
// ランデブーメッセージと相手のパケットに振り分けるのは ReceiveRouter が担う。
#pragma once

#include <cstdint>
#include <span>

#include "vocroz/network/udp_address.h"

namespace vocroz::network {

/// 受信したデータグラムの受け取り手。
class DatagramListener {
public:
    virtual ~DatagramListener() = default;

    /// データグラムを 1 つ受け取る。
    /// @param from     送信元のアドレス
    /// @param datagram UDP のペイロード。呼び出しから戻った後は参照しない
    /// スレッド安全性: ネットワーク受信スレッドから呼ばれる。受信を止めないよう、すぐに戻る。
    virtual void on_datagram(const UdpAddress& from,
                             std::span<const std::uint8_t> datagram) noexcept = 0;
};

}  // namespace vocroz::network
