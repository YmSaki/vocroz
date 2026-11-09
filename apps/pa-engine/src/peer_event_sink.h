// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機との接続の出来事の受け取り手。HostPeerLinks が session の HostSession へ知らせる口で、
// 組み立てでは SessionPeerEvents が HostSession へそのまま渡す。HostSession は PeerLink
// （HostPeerLinks）を受け取って作るので、通知の向きはこのインターフェースで切り離す。
#pragma once

#include <cstdint>
#include <span>

#include "vocroz/control_protocol/states.h"
#include "vocroz/session/identifiers.h"

namespace vocroz::engine {

/// 子機との接続の出来事。各関数の意味は HostSession の同名の関数のとおり。
/// スレッド安全性: コントロールスレッドから呼ばれる。
class PeerEventSink {
public:
    virtual ~PeerEventSink() = default;

    /// 制御メッセージが届いた。
    virtual void on_peer_message(session::PeerHandle peer,
                                 std::span<const std::uint8_t> message) = 0;
    /// 承認した接続のメディアを受け付け始めた。
    virtual void on_peer_established(session::PeerHandle peer) = 0;
    /// 承認した接続のメディアを受け付けられなかった。
    virtual void on_peer_path_failed(session::PeerHandle peer) = 0;
    /// 接続から 5 秒受信が途絶えた。
    virtual void on_peer_timeout(session::PeerHandle peer) = 0;
    /// 途絶えていた接続から、またパケットが届いた。
    virtual void on_peer_resumed(session::PeerHandle peer) = 0;
    /// 接続が閉じた（同じ送信元が張り直した承認前の接続）。
    virtual void on_peer_closed(session::PeerHandle peer) = 0;
    /// 承認済みの接続の信号状態が変わった。
    virtual void on_signal_state(session::PeerHandle peer, control_protocol::SignalState state) = 0;
};

}  // namespace vocroz::engine
