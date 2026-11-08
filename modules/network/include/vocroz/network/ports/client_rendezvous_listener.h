// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機の経路の確立（ClientRendezvous）が失敗したことを知らせるリスナー。
// 成功は鍵交換の応答が届いたことで組み立て側が判断するので、ここでは失敗だけを知らせる。
#pragma once

#include "vocroz/network/network_error.h"

namespace vocroz::network {

/// 子機の経路の確立の失敗の受け取り手。
class ClientRendezvousListener {
public:
    virtual ~ClientRendezvousListener() = default;

    /// 経路を確立できなかった。ClientRendezvous は止まっている。
    /// @param cause
    /// kRendezvousUnreachable（サーバーから応答がない）、kRendezvousCapacity（サーバーの上限）、
    ///              kHostNotFound（親機が登録していない）、kUnreachable（直結もホールパンチも成立しない）
    /// スレッド安全性: ClientRendezvous の poll() か on_rendezvous_datagram()
    /// を呼んだスレッドから、 その呼び出しの中で呼ばれる。
    virtual void on_path_failed(NetworkError cause) = 0;
};

}  // namespace vocroz::network
