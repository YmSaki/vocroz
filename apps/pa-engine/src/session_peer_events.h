// SPDX-License-Identifier: AGPL-3.0-only
//
// PeerEventSink を session の HostSession へそのまま渡す。HostSession は PeerLink を受け取って
// 作るので、作ったあとで bind() でつなぐ。
#pragma once

#include "vocroz/session/host_session.h"

#include "peer_event_sink.h"

namespace vocroz::engine {

/// 接続の出来事を HostSession へ渡す。
/// スレッド安全性: コントロールスレッドから呼ぶ。
class SessionPeerEvents final : public PeerEventSink {
public:
    /// 渡し先を決める。決めるまでの出来事は捨てる（HostSession を作る前に子機はつながらない）。
    /// @param session 渡し先。このオブジェクトの利用が終わるまで生存させる。null で外す
    void bind(session::HostSession* session) noexcept {
        session_ = session;
    }

    void on_peer_message(session::PeerHandle peer, std::span<const std::uint8_t> message) override;
    void on_peer_established(session::PeerHandle peer) override;
    void on_peer_path_failed(session::PeerHandle peer) override;
    void on_peer_timeout(session::PeerHandle peer) override;
    void on_peer_resumed(session::PeerHandle peer) override;
    void on_peer_closed(session::PeerHandle peer) override;
    void on_signal_state(session::PeerHandle peer, control_protocol::SignalState state) override;

private:
    session::HostSession* session_ = nullptr;
};

}  // namespace vocroz::engine
