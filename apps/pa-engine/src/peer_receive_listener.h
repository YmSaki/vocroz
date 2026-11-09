// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機のパケットの受信の処理（PeerReceivePath）から、コントロールスレッドの処理
// （HostPeerLinks）への通知。メディアは受信スレッドのままジッターバッファへ入れ、
// 状態を持つ処理（鍵交換、信頼制御層、session への通知）に要るものだけをここで渡す
// （docs/adr/0028-engine-assembly.md）。
// 受信スレッドとコントロールスレッドの間は PeerReceiveRelay がループに積んでつなぎ、
// HostPeerLinks は同じインターフェースをコントロールスレッドで受け取る。
#pragma once

#include <cstdint>
#include <span>

#include "vocroz/network/udp_address.h"
#include "vocroz/session/identifiers.h"
#include "vocroz/transport/signal_state.h"

namespace vocroz::engine {

/// 子機のパケットの受信の通知。
/// スレッド安全性: 呼ぶスレッドは実装ごとに決める（PeerReceiveRelay は受信スレッドから、
/// HostPeerLinks はコントロールスレッドから呼ばれる）。引数の範囲は呼び出しの間だけ有効。
class PeerReceiveListener {
public:
    virtual ~PeerReceiveListener() = default;

    /// 鍵交換の 1 つ目のメッセージが届いた。認証前なので、検証は受け取った側で行う。
    /// @param from     送信元
    /// @param datagram 受信したパケット全体
    virtual void on_handshake_initiation(const network::UdpAddress& from,
                                         std::span<const std::uint8_t> datagram) = 0;

    /// 認証済みの制御パケットが届いた。
    /// @param peer      接続
    /// @param plaintext 復号した平文（信頼制御層のフレーム）
    virtual void on_control_packet(session::PeerHandle peer,
                                   std::span<const std::uint8_t> plaintext) = 0;

    /// 承認済みの接続の信号状態が変わった。
    /// @param peer  接続
    /// @param state 新しい信号状態
    virtual void on_signal_state(session::PeerHandle peer, transport::SignalState state) = 0;

    /// 取り外しを指示した接続を、受信の処理がもう参照しなくなった。
    /// @param peer 接続
    virtual void on_receive_released(session::PeerHandle peer) = 0;
};

}  // namespace vocroz::engine
