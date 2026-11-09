// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機のセッション管理の公開 API の実装。
// 状態と共通の副作用は HostContext、UI のコマンドは HostCommandHandler、子機からの通知は
// HostPeerHandler が持ち、ここでは起動、ランデブーサーバー、出力デバイスの抜去、時間経過の
// 処理と振り分けだけを行う。
// 状態の遷移は src/state/ の遷移表に従う。

#include "vocroz/session/host_session.h"

#include <utility>

#include "vocroz/session/invite_code.h"

#include "detail/text_utils.h"
#include "host_command_handler.h"
#include "host_context.h"
#include "host_peer_handler.h"
#include "session_constants.h"
#include "state/output_device_state_machine.h"
#include "state/session_state_machine.h"

namespace vocroz::session {
namespace {

namespace cp = control_protocol;
using cp::ConnectionState;
using cp::ErrorCode;
using cp::SessionState;
using state::ConnectionEvent;
using state::SessionEvent;

}  // namespace

class HostSession::Impl {
public:
    Impl(Config config, Ports ports) : ctx(std::move(config), ports) {}

    void start() {
        auto loaded = ctx.ports.settings.load_host();
        if (loaded) {
            ctx.settings = std::move(loaded).value();
        } else {
            ctx.log(loaded.error() == SettingsError::kNotFound ? common::LogLevel::kInfo
                                                               : common::LogLevel::kWarning,
                    "親機の設定を読めなかったため、既定値で起動します");
        }
        if (!detail::is_displayable_text(ctx.settings.session_name, kMaxSessionNameBytes)) {
            ctx.settings.session_name.clear();
        }
        ctx.settings.master_gain_db = clamp_fader(ctx.settings.master_gain_db);
        ctx.master.gain_db = ctx.settings.master_gain_db;
        // 保存した値がどうであれ、Master はミュートで始める（設定の破損時も同じ）。
        ctx.ports.mixer.set_master_muted(true);
        ctx.ports.mixer.set_master_gain(ctx.master.gain_db);
        ctx.restore_routing();
        ctx.refresh_output_devices();
        open_saved_output_device();
    }

    // 保存した ID のデバイスだけを開く。開けなければ未選択のままにし、ほかのデバイスへは
    // 切り替えない（出力デバイスを暗黙にフォールバックしない）。保存した ID は消さず、
    // 次の起動でまた試す。
    void open_saved_output_device() {
        if (!ctx.settings.output_device_id) {
            return;
        }
        const auto id = *ctx.settings.output_device_id;
        if (!ctx.open_output(id)) {
            ctx.log(common::LogLevel::kWarning, "保存した出力デバイスを開けませんでした");
        }
    }

    // --- ランデブーサーバー ---

    void on_invite_registered(const InviteToken& token,
                              const common::Result<RegisteredInvite, ErrorCode>& result) {
        if (!ctx.invites.is_issuing(token)) {
            // 終了や発行し直しで不要になった要求の結果。登録できていたら、すぐに失効させる。
            if (result) {
                ctx.ports.rendezvous.revoke_invite(result.value().code, result.value().revoke_key);
            }
            return;
        }
        if (!result) {
            ctx.invites.cancel_issue();
            if (ctx.session.state == SessionState::kCreating) {
                ctx.session.error_code = result.error();
                ctx.advance_session(SessionEvent::kFault);
                ctx.mute_master(cp::MasterMuteReason::kSafety);
            } else {
                // 発行し直しの失敗。参加済みの子機には影響しないので、間隔を置いて再試行する。
                ctx.next_issue_at = ctx.ports.clock.now() + kInviteRetryInterval;
                ctx.log(common::LogLevel::kWarning, "招待を発行し直せませんでした。再試行します");
            }
            ctx.publish_session();
            return;
        }
        ctx.invites.complete_issue(token, result.value(), ctx.ports.clock.now() + kInviteLifetime);
        ctx.session.invite_url = make_invite_url(ctx.config.invite_base_url, result.value().code);
        if (ctx.session.state == SessionState::kCreating) {
            ctx.advance_session(SessionEvent::kCreated);
        }
        ctx.publish_session();
    }

    // --- 出力デバイス ---

    void on_output_device_lost() {
        // 抜けたデバイスの後始末。状態が OPEN でなくても閉じておく（閉じるのは冪等）。
        ctx.ports.outputs.close_output();
        if (!ctx.advance_output(state::OutputDeviceEvent::kLost)) {
            return;
        }
        // ID は残し、挿し直したデバイスを選び直しやすくする。
        ctx.session.output_device_name.reset();
        ctx.refresh_output_devices();
        // 別のデバイスへは流さず、Master を止めてオペレーターの選択を待つ（フェイルセーフ）。
        ctx.mute_master(cp::MasterMuteReason::kSafety);
        ctx.publish_session();
        ctx.notice(cp::NoticeSeverity::kWarning, "output_lost", {});
    }

    void on_engine_fault(ErrorCode code) {
        if (!state::next_state(ctx.session.state, SessionEvent::kFault)) {
            return;
        }
        ctx.session.error_code = code;
        ctx.advance_session(SessionEvent::kFault);
        ctx.mute_master(cp::MasterMuteReason::kSafety);
        ctx.publish_session();
    }

    void revoke_invite() {
        if (!ctx.accepting_joins()) {
            return;
        }
        if (auto revoked = ctx.invites.revoke_current()) {
            ctx.ports.rendezvous.revoke_invite(revoked->code, revoked->revoke_key);
        }
        ctx.session.invite_url.reset();
        ctx.issue_invite();
        ctx.publish_session();
    }

    void tick() {
        const auto now = ctx.ports.clock.now();
        if (ctx.accepting_joins()) {
            bool changed = false;
            if (ctx.invites.expire_if_due(now)) {
                // 期限切れの招待はサーバーでも解決できなくなるので、新しい招待に差し替える。
                ctx.session.invite_url.reset();
                ctx.issue_invite();
                changed = true;
            } else if (ctx.invites.current() == nullptr && !ctx.invites.issuing() &&
                       (!ctx.next_issue_at || now >= *ctx.next_issue_at)) {
                ctx.issue_invite();
            }
            if (changed) {
                ctx.publish_session();
            }
        }
        for (auto& p : ctx.registry.participants()) {
            if (p.strip.connection_state == ConnectionState::kReconnecting &&
                p.reconnecting_since && now - *p.reconnecting_since >= kReconnectGrace) {
                // 同じ子機のストリップはそろって LOST になるので、通知は 1 回だけになる。
                ctx.advance_participant(p, ConnectionEvent::kGraceExpired);
                ctx.release_peer(p);
                ctx.notice(cp::NoticeSeverity::kWarning, "participant_lost", label(p.strip));
            }
        }
    }

    HostContext ctx;
    // ハンドラーは ctx を参照するので、ctx より後に宣言して先に破棄されるようにする。
    HostCommandHandler commands{ctx};
    HostPeerHandler peers{ctx};
};

HostSession::HostSession(Config config, Ports ports)
    : impl_(std::make_unique<Impl>(std::move(config), ports)) {}

HostSession::~HostSession() = default;

void HostSession::start() {
    impl_->start();
}

void HostSession::handle_ui_message(const control_protocol::UiToEngineMessage& message) {
    impl_->commands.handle_ui_message(message);
}

void HostSession::on_invite_registered(
    const InviteToken& token,
    const common::Result<RegisteredInvite, control_protocol::ErrorCode>& result) {
    impl_->on_invite_registered(token, result);
}

void HostSession::on_peer_message(PeerHandle peer, std::span<const std::uint8_t> message) {
    impl_->peers.on_peer_message(peer, message);
}

void HostSession::on_peer_established(PeerHandle peer) {
    impl_->peers.on_peer_established(peer);
}

void HostSession::on_peer_path_failed(PeerHandle peer) {
    impl_->peers.on_peer_path_failed(peer);
}

void HostSession::on_peer_timeout(PeerHandle peer) {
    impl_->peers.on_peer_timeout(peer);
}

void HostSession::on_peer_resumed(PeerHandle peer) {
    impl_->peers.on_peer_resumed(peer);
}

void HostSession::on_peer_closed(PeerHandle peer) {
    impl_->peers.on_peer_closed(peer);
}

void HostSession::on_signal_state(PeerHandle peer, control_protocol::SignalState state) {
    impl_->peers.on_signal_state(peer, state);
}

void HostSession::on_output_device_lost() {
    impl_->on_output_device_lost();
}

void HostSession::on_engine_fault(control_protocol::ErrorCode code) {
    impl_->on_engine_fault(code);
}

void HostSession::revoke_invite() {
    impl_->revoke_invite();
}

void HostSession::tick() {
    impl_->tick();
}

control_protocol::MixerSnapshot HostSession::snapshot() const {
    return impl_->ctx.snapshot();
}

}  // namespace vocroz::session
