// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機のセッションの状態と、共通の副作用の実装。
// revision は「コマンドで変えられる値」が変わったときだけ進め、接続状態や信号状態のような
// 観測値の変化では進めない（docs/adr/0015-control-protocol-messages.md）。

#include "host_context.h"

#include <tuple>
#include <utility>

#include "detail/revision_utils.h"
#include "detail/token_utils.h"
#include "wire/peer_message_codec.h"

namespace vocroz::session {
namespace {

namespace cp = control_protocol;
using cp::ConnectionState;
using cp::ErrorCode;
using cp::SessionState;
using state::ConnectionEvent;
using state::SessionEvent;

}  // namespace

HostContext::HostContext(HostSession::Config config_value, HostSession::Ports ports_value)
    : config(std::move(config_value)), ports(ports_value) {
    session.entity_id = kSessionEntityId;
    session.revision = 1;
    master.entity_id = kMasterEntityId;
    master.revision = 1;
    // 初期ミュート: Master はミュートで始める。
    master.master_muted = true;
    master.master_mute_reason = cp::MasterMuteReason::kOperator;
}

cp::MixerSnapshot HostContext::snapshot() const {
    cp::MixerSnapshot snapshot;
    snapshot.session = session;
    snapshot.master = master;
    for (const auto& p : registry.participants()) {
        snapshot.channels.push_back(p.strip);
    }
    snapshot.pending = registry.pending_entries();
    snapshot.routing = routing.info();
    return snapshot;
}

std::vector<std::string> HostContext::strip_ids() const {
    std::vector<std::string> ids;
    ids.reserve(registry.participants().size());
    for (const auto& p : registry.participants()) {
        ids.push_back(p.strip.entity_id);
    }
    return ids;
}

void HostContext::forget_strip_in_routing(const std::string& strip_id) {
    if (routing.forget_strip(strip_id, strip_ids())) {
        const auto& info = routing.info();
        // 参照を取り除いただけなのでループは増えず、承認の状態はそのまま引き継げる。
        if (!ports.mixer.apply_routing(info.applied, info.feedback_approved)) {
            log(common::LogLevel::kWarning,
                "ストリップを削除したあとのルーティングをミキサーに適用できませんでした");
        }
    }
    publish_routing();
}

void HostContext::deny(PeerHandle peer, ErrorCode reason) {
    send(peer, wire::JoinDenied{reason});
    ports.peers.close(peer);
}

// 参加者と接続の結び付きを解く。ストリップと再接続トークンは残す。
void HostContext::release_peer(Participant& p) {
    if (p.peer) {
        ports.peers.close(*p.peer);
        p.peer.reset();
    }
}

void HostContext::send(PeerHandle peer, const wire::HostToClientMessage& message) {
    const auto bytes = wire::encode(message);
    ports.peers.send(peer, bytes);
}

// --- 状態の遷移 ---

bool HostContext::advance_session(SessionEvent event) {
    const auto next = state::next_state(session.state, event);
    if (!next) {
        return false;
    }
    session.state = *next;
    return true;
}

bool HostContext::advance_output(state::OutputDeviceEvent event) {
    const auto next = state::next_state(session.output_device_state, event);
    if (!next) {
        return false;
    }
    session.output_device_state = *next;
    return true;
}

// 接続状態を進め、それに伴う値（信号状態、猶予の起点、原因）をそろえる。
bool HostContext::advance_connection(Participant& p, ConnectionEvent event) {
    const auto next = state::next_state(p.strip.connection_state, event);
    if (!next) {
        return false;
    }
    p.strip.connection_state = *next;
    p.strip.signal_state.reset();
    switch (*next) {
        case ConnectionState::kConnected:
            // 信号状態は transport の判定が届くまで、送信ミュートの通知から決める。
            p.strip.signal_state =
                p.strip.send_muted ? cp::SignalState::kPaused : cp::SignalState::kResuming;
            p.strip.error_code.reset();
            p.reconnecting_since.reset();
            break;
        case ConnectionState::kReconnecting:
            if (!p.reconnecting_since) {
                p.reconnecting_since = ports.clock.now();
            }
            break;
        case ConnectionState::kConnecting:
            p.strip.error_code.reset();
            break;
        case ConnectionState::kFailed:
            p.strip.error_code = ErrorCode::kUnreachable;
            p.reconnecting_since.reset();
            break;
        case ConnectionState::kLost:
            p.reconnecting_since.reset();
            break;
        case ConnectionState::kPending:
            break;
    }
    // 接続状態は観測値なので revision を進めない。
    publish_channel(p);
    update_session_activity();
    return true;
}

// CONNECTED か RECONNECTING の参加者の有無で、READY と ACTIVE を切り替える。
// 全員が LOST や FAILED になったら READY に戻る。
void HostContext::update_session_activity() {
    const bool active = registry.any_active();
    const auto event =
        active ? SessionEvent::kParticipantActive : SessionEvent::kNoParticipantActive;
    if (advance_session(event)) {
        publish_session();
    }
}

bool HostContext::accepting_joins() const noexcept {
    return session.state == SessionState::kReady || session.state == SessionState::kActive;
}

void HostContext::issue_invite() {
    const auto token = detail::random_bytes<kInviteTokenBytes>(ports.random);
    invites.begin_issue(token);
    ports.rendezvous.register_invite(
        InviteRegistration{config.host_public_key, token, rendezvous_id});
}

// 招待を失効させ、すべての子機を切断し、ストリップを片付ける。
void HostContext::teardown() {
    if (auto revoked = invites.revoke_current()) {
        ports.rendezvous.revoke_invite(revoked->code, revoked->revoke_key);
    }
    invites.clear();
    next_issue_at.reset();
    for (const auto& pending : registry.pending_entries()) {
        if (const auto* p = registry.find_pending(pending.entity_id)) {
            deny(p->peer, ErrorCode::kSessionEnded);
        }
    }
    // Master を先にミュートし、ミキサーのランプでフェードアウトさせてからチャンネルを外す。
    mute_master(cp::MasterMuteReason::kOperator);
    const auto removed = strip_ids();
    for (auto& p : registry.participants()) {
        if (p.peer) {
            send(*p.peer, wire::SessionEndedNotice{});
            ports.peers.close(*p.peer);
        }
        ports.mixer.remove_channel(p.strip.entity_id);
        ports.ui.publish(cp::ChannelRemoved{p.strip.entity_id});
    }
    const bool had_pending = registry.pending_count() > 0;
    registry.clear();
    if (had_pending) {
        publish_pending();
    }
    // バスや出力の構成はセッションをまたいで残し、消えたストリップへの参照だけを片付ける。
    for (const auto& strip_id : removed) {
        forget_strip_in_routing(strip_id);
    }
    save_settings();
}

// Master をミュートする。理由が変わるだけでも revision を進め、古い表示に基づく解除を
// REVISION_CONFLICT で拒否できるようにする。
void HostContext::mute_master(cp::MasterMuteReason reason) {
    if (master.master_muted && master.master_mute_reason == reason) {
        return;
    }
    master.master_muted = true;
    master.master_mute_reason = reason;
    ports.mixer.set_master_muted(true);
    detail::bump_revision(master.revision);
    publish_master();
}

void HostContext::save_settings() {
    if (const auto saved = ports.settings.save_host(settings); !saved) {
        log(common::LogLevel::kWarning, "親機の設定を保存できませんでした");
    }
}

// --- UI への通知 ---

void HostContext::publish_session() {
    ports.ui.publish(cp::SessionUpdated{session});
}

void HostContext::publish_master() {
    ports.ui.publish(cp::MasterUpdated{master});
}

void HostContext::publish_channel(const Participant& p) {
    ports.ui.publish(cp::ChannelUpdated{p.strip});
}

void HostContext::publish_pending() {
    ports.ui.publish(cp::PendingUpdated{registry.pending_entries()});
}

void HostContext::publish_routing() {
    ports.ui.publish(cp::RoutingUpdated{routing.info()});
}

void HostContext::publish_route_level(const cp::RouteLevel& level) {
    ports.ui.publish(cp::RouteLevelUpdated{level});
}

void HostContext::notice(cp::NoticeSeverity severity, const char* code, const std::string& name) {
    cp::Notice n;
    n.severity = severity;
    n.code = code;
    if (!name.empty()) {
        n.params.emplace("name", name);
    }
    ports.ui.publish(n);
}

void HostContext::log(common::LogLevel level, const char* message) {
    ports.logger.log(level, message);
}

}  // namespace vocroz::session
