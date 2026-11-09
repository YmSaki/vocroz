// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機のランデブーサーバーへの登録の状態（HostRendezvous）の変化を知らせるリスナー。
// 登録できない間は新しい子機が参加できないので、親機の画面に警告を出すのに使う。
// 確立済みの接続はサーバーに依存しないので、登録できなくなっても切断しない。
#pragma once

#include "vocroz/network/network_error.h"

namespace vocroz::network {

/// 親機の登録の状態の受け取り手。
class HostRendezvousListener {
public:
    virtual ~HostRendezvousListener() = default;

    /// 登録できた。登録できない状態から回復したときにも呼ばれる。
    /// スレッド安全性: HostRendezvous の poll() か on_rendezvous_datagram() を呼んだスレッドから、
    /// その呼び出しの中で呼ばれる。
    virtual void on_rendezvous_available() = 0;

    /// 登録できなくなった。HostRendezvous は送り直しを続ける。
    /// @param cause kRendezvousUnreachable（サーバーから応答がない）か
    /// kRendezvousCapacity（サーバーの上限） スレッド安全性: on_rendezvous_available() と同じ。
    virtual void on_rendezvous_unavailable(NetworkError cause) = 0;
};

}  // namespace vocroz::network
