// SPDX-License-Identifier: AGPL-3.0-only
//
// ネットワーク受信スレッドの通知（PeerReceiveListener）を、コントロールループに積んで
// コントロールスレッドの受け取り手（HostPeerLinks）へ渡す。session と信頼制御層は
// コントロールスレッドだけで動かすため（AGENTS.md 2.3、docs/adr/0028-engine-assembly.md）。
// 引数の範囲は呼び出しの間だけ有効なので、積むときに写す。鍵交換と制御パケットは
// 毎秒数個なので、写すときのヒープの確保は受信スレッドの許す範囲に収まる（メディアはここを通らない）。
#pragma once

#include <atomic>

#include "control_loop.h"
#include "peer_receive_listener.h"

namespace vocroz::engine {

/// 受信スレッドの通知をコントロールスレッドへ渡す中継。
/// スレッド安全性: set_target() はコントロールスレッドから呼ぶ。通知はネットワーク受信スレッドから
/// 呼ばれ、受け取り手はコントロールスレッドで呼ばれる。
class PeerReceiveRelay final : public PeerReceiveListener {
public:
    /// @param loop 通知を積むループ。このオブジェクトより長く生存させる
    explicit PeerReceiveRelay(ControlLoop& loop) noexcept;

    /// コントロールスレッドの受け取り手を決める。決めるまでの通知は捨てる。
    /// @param target 受け取り手。ループが止まるまで生存させる
    void set_target(PeerReceiveListener* target) noexcept {
        target_.store(target, std::memory_order_release);
    }

    void on_handshake_initiation(const network::UdpAddress& from,
                                 std::span<const std::uint8_t> datagram) override;
    void on_control_packet(session::PeerHandle peer,
                           std::span<const std::uint8_t> plaintext) override;
    void on_signal_state(session::PeerHandle peer, transport::SignalState state) override;
    void on_receive_released(session::PeerHandle peer) override;

private:
    ControlLoop& loop_;
    // 受信スレッドは受け取り手を作るより先に動き出すので、決まるまでの通知を捨てられるよう
    // アトミックに読む。
    std::atomic<PeerReceiveListener*> target_{nullptr};
};

}  // namespace vocroz::engine
