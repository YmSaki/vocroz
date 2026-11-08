// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機からの制御メッセージと、子機との接続の状態変化の処理。
// 参加の申請、再接続、送信ミュートと入力の報告、退出をここで扱い、
// 状態の変更と副作用は HostContext に任せる。
#pragma once

#include <cstdint>
#include <span>

#include "vocroz/control_protocol/entities.h"
#include "vocroz/session/identifiers.h"

#include "host_context.h"
#include "wire/peer_messages.h"

namespace vocroz::session {

/// 子機ごとの接続（PeerHandle）から届く通知を処理する。
/// 引数と意味は HostSession の同名の関数と同じ。
/// スレッド安全性: コントロールスレッドからだけ使う。
class HostPeerHandler {
public:
    /// @param context 親機のセッションの状態。このオブジェクトより長く生存すること。
    explicit HostPeerHandler(HostContext& context);

    void on_peer_message(PeerHandle peer, std::span<const std::uint8_t> bytes);
    void on_peer_established(PeerHandle peer);
    void on_peer_path_failed(PeerHandle peer);
    void on_peer_timeout(PeerHandle peer);
    void on_peer_resumed(PeerHandle peer);
    void on_peer_closed(PeerHandle peer);
    void on_signal_state(PeerHandle peer, control_protocol::SignalState signal);

private:
    void handle_join_request(PeerHandle peer, const wire::JoinRequest& request);
    void handle_reconnect(PeerHandle peer, const ReconnectToken& token);
    void rejoin(Participant& p, PeerHandle peer);
    void handle_send_mute(PeerHandle peer, const wire::SendMuteReport& report);
    void handle_input_report(PeerHandle peer, const wire::InputReport& report);
    void handle_peer_left(PeerHandle peer);

    HostContext& ctx_;
};

}  // namespace vocroz::session
