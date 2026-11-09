// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機のセッションの状態と、共通の副作用の実装。
// revision は「コマンドで変えられる値」が変わったときだけ進め、接続状態や信号状態のような
// 観測値の変化では進めない（docs/adr/0015-control-protocol-messages.md）。

#include "host_context.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include "vocroz/transport/srtp_master_key.h"

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

// 構成にフェーダーを持つバスかセンドとして entity_id があれば true。
bool has_route_level(const cp::RoutingConfig& config, const std::string& entity_id) {
    const auto has = [&](const auto& items) {
        return std::any_of(items.begin(), items.end(),
                           [&](const auto& item) { return item.entity_id == entity_id; });
    };
    return has(config.buses) || has(config.sends);
}

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
    snapshot.external_output = external_output.info();
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

common::Result<void, ErrorCode> HostContext::commit_staged_routing(bool approve_feedback) {
    if (auto prepared = routing.prepare_commit(approve_feedback, strip_ids()); !prepared) {
        return prepared;
    }
    const auto& staged = *routing.info().staged;
    if (!ports.mixer.apply_routing(staged.config, staged.check.needs_approval)) {
        // ミキサーが受け付けなければ今のグラフのまま音を出し続け、ステージングも残す。
        log(common::LogLevel::kWarning, "ルーティングをミキサーに適用できませんでした");
        return common::make_unexpected(ErrorCode::kEngineError);
    }
    routing.commit();
    stop_external_output_without_source();
    return {};
}

bool HostContext::is_external_output_source(const std::string& entity_id) const {
    if (entity_id == kMasterEntityId) {
        return true;
    }
    const auto& buses = routing.info().applied.buses;
    return std::any_of(buses.begin(), buses.end(),
                       [&](const cp::RoutingBus& bus) { return bus.entity_id == entity_id; });
}

void HostContext::restore_external_output() {
    std::optional<transport::SrtpMasterKey> key;
    if (auto stored = ports.secrets.load(SecretSlot::kExternalOutputKey)) {
        auto& bytes = stored.value();
        const std::string text(bytes.begin(), bytes.end());
        std::fill(bytes.begin(), bytes.end(), std::uint8_t{0});
        if (auto parsed = transport::parse_srtp_master_key(text)) {
            key = std::move(parsed).value();
        } else {
            log(common::LogLevel::kWarning,
                "保存した外部出力の鍵が壊れているため、鍵を未設定にして起動します");
        }
    } else if (stored.error() != SecretError::kNotFound) {
        log(common::LogLevel::kWarning,
            "外部出力の鍵を読めなかったため、鍵を未設定にして起動します");
    }
    external_output.restore(settings.external_output, std::move(key));
}

void HostContext::stop_external_output_without_source() {
    const auto& info = external_output.info();
    if (info.state != cp::ExternalOutputState::kRunning ||
        is_external_output_source(info.config.source_id)) {
        return;
    }
    // 送り出すバスがなくなったら送信を止める。別のバスへ黙って切り替えない。
    ports.external_output.stop();
    external_output.record_stop();
    publish_external_output();
    notice(cp::NoticeSeverity::kWarning, "external_output_source_removed", {});
}

void HostContext::apply_latency_target(double target_ms) {
    session.latency_target_ms = target_ms;
    settings.latency_target_ms = target_ms;
    ports.playout.set_latency_target(
        std::chrono::microseconds{static_cast<std::int64_t>(std::llround(target_ms * 1000.0))});
}

void HostContext::restore_routing() {
    auto restored = without_strip_references(settings.routing);
    if (restored == cp::RoutingConfig{}) {
        return;
    }
    // 起動時にはストリップがないので、ストリップのない状態で検証する。
    routing.stage(std::move(restored), {});
    const auto& check = routing.info().staged->check;
    if (check.issue) {
        // 壊れた構成や制限の変わった構成では音を出さない（フェイルセーフ）。既定の結線で起動する。
        std::ignore = routing.discard();
        log(common::LogLevel::kWarning,
            "保存したルーティングに問題があるため、既定の結線で起動します");
        return;
    }
    if (check.needs_approval) {
        // ループの承認は適用 1 回ごとに求めるので、保存した構成でも自動では適用しない。
        log(common::LogLevel::kInfo, "保存したルーティングはループの承認を待っています");
        return;
    }
    if (const auto committed = commit_staged_routing(false); !committed) {
        std::ignore = routing.discard();
        return;
    }
    for (const auto& saved : settings.route_gains) {
        if (auto* level = routing.find_level(saved.entity_id)) {
            level->gain_db = clamp_fader(saved.gain_db);
            ports.mixer.set_route_gain(level->entity_id, level->gain_db);
        }
    }
}

void HostContext::deny(PeerHandle peer, ErrorCode reason) {
    send(peer, wire::JoinDenied{reason});
    ports.peers.close(peer);
}

// 参加者と接続の結び付きを解く。ストリップと再接続トークンは残す。
void HostContext::release_peer(Participant& p) {
    if (!p.peer) {
        return;
    }
    const PeerHandle peer = *p.peer;
    ports.peers.close(peer);
    for (Participant* strip : registry.strips_of(p.strip.participant_id)) {
        if (strip->peer == peer) {
            strip->peer.reset();
        }
    }
}

void HostContext::admit_streams(PeerHandle peer, const std::string& participant_id) {
    std::vector<StreamRoute> routes;
    for (const Participant* strip : registry.strips_of(participant_id)) {
        routes.push_back(
            StreamRoute{strip->stream_id, strip->strip.channel_count, strip->strip.entity_id});
    }
    ports.peers.admit(peer, routes);
}

void HostContext::remove_strip(const std::string& strip_id) {
    ports.mixer.remove_channel(strip_id);
    registry.remove(strip_id);
    ports.ui.publish(cp::ChannelRemoved{strip_id});
    forget_strip_in_routing(strip_id);
}

void HostContext::remove_participant_strips(const std::string& participant_id) {
    std::vector<std::string> ids;
    for (const Participant* strip : registry.strips_of(participant_id)) {
        ids.push_back(strip->strip.entity_id);
    }
    for (const auto& id : ids) {
        remove_strip(id);
    }
}

void HostContext::sync_return(const Participant& p) {
    if (!p.peer) {
        return;
    }
    ports.peers.set_return_mute(*p.peer, p.strip.return_muted, p.return_epoch);
    send(*p.peer, wire::ReturnMuteNotice{p.strip.return_muted, p.return_epoch});
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

// --- 出力デバイス ---

void HostContext::close_output() {
    ports.outputs.close_output();
    // NONE ならすでに閉じている。LOST から閉じるのは、抜けたデバイスの選択をやめるとき。
    advance_output(state::OutputDeviceEvent::kClosed);
    session.output_device_name.reset();
    session.output_device_id.reset();
}

bool HostContext::open_output(const std::string& device_id) {
    // 2 つのデバイスから同時にミキサーを呼ばないよう、必ず閉じてから開く。
    close_output();
    auto opened = ports.outputs.open_output(device_id);
    if (!opened) {
        return false;
    }
    advance_output(state::OutputDeviceEvent::kOpened);
    session.output_device_name = std::move(opened).value();
    session.output_device_id = device_id;
    if (settings.output_device_id != device_id) {
        settings.output_device_id = device_id;
        save_settings();
    }
    return true;
}

void HostContext::refresh_output_devices() {
    session.output_devices = ports.outputs.list_outputs();
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

bool HostContext::advance_participant(Participant& p, ConnectionEvent event) {
    // 遷移で参照が変わらないよう、ストリップの一覧を先に取る（遷移では一覧を変えない）。
    const auto strips = registry.strips_of(p.strip.participant_id);
    bool advanced = false;
    for (Participant* strip : strips) {
        advanced = advance_connection(*strip, event) || advanced;
    }
    return advanced;
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
            // 同じ子機のストリップが複数あっても、終了の通知は 1 回だけ送る。
            send(*p.peer, wire::SessionEndedNotice{});
            release_peer(p);
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
    const auto& info = routing.info();
    settings.routing = without_strip_references(info.applied);
    settings.route_gains.clear();
    for (const auto& level : info.levels) {
        if (has_route_level(settings.routing, level.entity_id)) {
            settings.route_gains.push_back(SavedRouteGain{level.entity_id, level.gain_db});
        }
    }
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

void HostContext::publish_external_output() {
    ports.ui.publish(cp::ExternalOutputUpdated{external_output.info()});
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
