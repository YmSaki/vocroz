// SPDX-License-Identifier: AGPL-3.0-only
//
// SessionPeerEvents の実装。

#include "session_peer_events.h"

namespace vocroz::engine {

void SessionPeerEvents::on_peer_message(session::PeerHandle peer,
                                        std::span<const std::uint8_t> message) {
    if (session_ != nullptr) {
        session_->on_peer_message(peer, message);
    }
}

void SessionPeerEvents::on_peer_established(session::PeerHandle peer) {
    if (session_ != nullptr) {
        session_->on_peer_established(peer);
    }
}

void SessionPeerEvents::on_peer_path_failed(session::PeerHandle peer) {
    if (session_ != nullptr) {
        session_->on_peer_path_failed(peer);
    }
}

void SessionPeerEvents::on_peer_timeout(session::PeerHandle peer) {
    if (session_ != nullptr) {
        session_->on_peer_timeout(peer);
    }
}

void SessionPeerEvents::on_peer_resumed(session::PeerHandle peer) {
    if (session_ != nullptr) {
        session_->on_peer_resumed(peer);
    }
}

void SessionPeerEvents::on_peer_closed(session::PeerHandle peer) {
    if (session_ != nullptr) {
        session_->on_peer_closed(peer);
    }
}

void SessionPeerEvents::on_signal_state(session::PeerHandle peer,
                                        control_protocol::SignalState state) {
    if (session_ != nullptr) {
        session_->on_signal_state(peer, state);
    }
}

}  // namespace vocroz::engine
