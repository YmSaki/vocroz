// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の UI から届いたコマンドの適用の実装。
// 離散コマンドは「重複 → 対象の有無（NOT_FOUND）→ revision（REVISION_CONFLICT）→
// 種類と状態（INVALID_STATE）」の順に検査する。

#include "host_command_handler.h"

#include <type_traits>
#include <utility>
#include <variant>

#include "detail/revision_utils.h"
#include "detail/token_utils.h"

namespace vocroz::session {
namespace {

namespace cp = control_protocol;
using cp::ConnectionState;
using cp::ErrorCode;
using cp::SessionState;
using state::SessionEvent;

}  // namespace

HostCommandHandler::HostCommandHandler(HostContext& context) : ctx_(context) {}

void HostCommandHandler::handle_ui_message(const cp::UiToEngineMessage& message) {
    std::visit(
        [this](const auto& m) {
            using M = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<M, cp::Hello>) {
                ctx_.ports.ui.publish(cp::MixerSnapshotEvent{ctx_.snapshot()});
            } else if constexpr (std::is_same_v<M, cp::DiscreteCommand>) {
                handle_discrete(m);
            } else {
                static_assert(std::is_same_v<M, cp::SetGain>);
                handle_gain(m);
            }
        },
        message);
}

// --- 離散コマンド ---

void HostCommandHandler::handle_discrete(const cp::DiscreteCommand& command) {
    if (!requests_.remember(command.request_id)) {
        return;
    }
    const auto result = apply(command);
    if (result) {
        ctx_.ports.ui.publish(cp::Ack{command.request_id, result.value()});
    } else {
        ctx_.ports.ui.publish(cp::Reject{command.request_id, result.error()});
    }
}

// entity の今の revision を返す。entity がなければ std::nullopt。
std::optional<cp::Revision> HostCommandHandler::revision_of(const std::string& entity_id) {
    if (entity_id == kSessionEntityId) {
        return ctx_.session.revision;
    }
    if (entity_id == kMasterEntityId) {
        return ctx_.master.revision;
    }
    if (const auto* p = ctx_.registry.find(entity_id)) {
        return p->strip.revision;
    }
    if (const auto* pending = ctx_.registry.find_pending(entity_id)) {
        return pending->entry.revision;
    }
    return std::nullopt;
}

HostCommandHandler::CommandResult HostCommandHandler::apply(const cp::DiscreteCommand& command) {
    const auto revision = revision_of(command.entity_id);
    if (!revision) {
        return common::make_unexpected(ErrorCode::kNotFound);
    }
    if (*revision != command.expected_revision) {
        return common::make_unexpected(ErrorCode::kRevisionConflict);
    }
    const auto& id = command.entity_id;
    // 認可: コマンドごとに対象にできる entity の種類を決め、それ以外は INVALID_STATE で拒む。
    // 子機のコマンド（Join など）は親機では受け付けない。
    return std::visit(
        [&](const auto& body) -> CommandResult {
            using B = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<B, cp::CreateSession>) {
                return id == kSessionEntityId ? create_session() : invalid_state();
            } else if constexpr (std::is_same_v<B, cp::EndSession>) {
                return id == kSessionEntityId ? end_session() : invalid_state();
            } else if constexpr (std::is_same_v<B, cp::SetChannelMute>) {
                return set_channel_mute(id, body.muted);
            } else if constexpr (std::is_same_v<B, cp::SetMasterMute>) {
                return id == kMasterEntityId ? set_master_mute(body.muted) : invalid_state();
            } else if constexpr (std::is_same_v<B, cp::ApproveParticipant>) {
                return approve(id);
            } else if constexpr (std::is_same_v<B, cp::RejectParticipant>) {
                return reject(id);
            } else if constexpr (std::is_same_v<B, cp::RemoveStrip>) {
                return remove_strip(id);
            } else {
                static_assert(std::is_same_v<B, cp::Join> || std::is_same_v<B, cp::Leave> ||
                              std::is_same_v<B, cp::SelectInput> ||
                              std::is_same_v<B, cp::SetSendMute>);
                return invalid_state();
            }
        },
        command.body);
}

HostCommandHandler::CommandResult HostCommandHandler::invalid_state() {
    return common::make_unexpected(ErrorCode::kInvalidState);
}

HostCommandHandler::CommandResult HostCommandHandler::create_session() {
    if (!ctx_.advance_session(SessionEvent::kCreate)) {
        return invalid_state();
    }
    ctx_.session.name = ctx_.settings.session_name;
    ctx_.session.error_code.reset();
    ctx_.session.invite_url.reset();
    ctx_.rendezvous_id = detail::random_bytes<std::tuple_size_v<RendezvousId>>(ctx_.ports.random);
    ctx_.next_issue_at.reset();
    detail::bump_revision(ctx_.session.revision);
    ctx_.publish_session();
    ctx_.issue_invite();
    return ctx_.session.revision;
}

HostCommandHandler::CommandResult HostCommandHandler::end_session() {
    if (ctx_.session.state == SessionState::kError) {
        // エラーの確認。残っている接続とストリップを片付けて IDLE へ戻る。
        ctx_.teardown();
        ctx_.advance_session(SessionEvent::kRecover);
    } else {
        if (!ctx_.advance_session(SessionEvent::kEnd)) {
            return invalid_state();
        }
        detail::bump_revision(ctx_.session.revision);
        ctx_.publish_session();
        ctx_.teardown();
        ctx_.advance_session(SessionEvent::kEnded);
    }
    ctx_.session.name.clear();
    ctx_.session.error_code.reset();
    ctx_.session.invite_url.reset();
    detail::bump_revision(ctx_.session.revision);
    ctx_.publish_session();
    return ctx_.session.revision;
}

HostCommandHandler::CommandResult HostCommandHandler::set_channel_mute(const std::string& entity_id,
                                                                       bool muted) {
    auto* p = ctx_.registry.find(entity_id);
    if (p == nullptr) {
        return invalid_state();
    }
    if (p->strip.channel_muted != muted) {
        p->strip.channel_muted = muted;
        ctx_.ports.mixer.set_channel_muted(p->strip.entity_id, muted);
        detail::bump_revision(p->strip.revision);
        ctx_.publish_channel(*p);
    }
    return p->strip.revision;
}

HostCommandHandler::CommandResult HostCommandHandler::set_master_mute(bool muted) {
    if (muted) {
        if (!ctx_.master.master_muted) {
            ctx_.mute_master(cp::MasterMuteReason::kOperator);
        }
    } else if (ctx_.master.master_muted) {
        // SAFETY のミュートも、解除できるのはオペレーターの明示的な操作だけ。
        ctx_.master.master_muted = false;
        ctx_.master.master_mute_reason.reset();
        ctx_.ports.mixer.set_master_muted(false);
        detail::bump_revision(ctx_.master.revision);
        ctx_.publish_master();
    }
    return ctx_.master.revision;
}

HostCommandHandler::CommandResult HostCommandHandler::approve(const std::string& entity_id) {
    const auto* pending = ctx_.registry.find_pending(entity_id);
    if (pending == nullptr) {
        return invalid_state();
    }
    if (ctx_.registry.participants().size() >= kMaxChannelStrips) {
        return invalid_state();
    }
    const auto revision = pending->entry.revision;
    const auto peer = pending->peer;
    const auto token = detail::random_bytes<std::tuple_size_v<ReconnectToken>>(ctx_.ports.random);
    auto& p = ctx_.registry.add_participant(*pending, token);
    ctx_.registry.remove_pending(entity_id);
    ctx_.ports.mixer.add_channel(p.strip.entity_id);
    ctx_.ports.peers.admit(peer, p.strip.entity_id);
    ctx_.send(peer, wire::JoinApproved{token});
    ctx_.publish_pending();
    ctx_.publish_channel(p);
    return revision;
}

HostCommandHandler::CommandResult HostCommandHandler::reject(const std::string& entity_id) {
    const auto* pending = ctx_.registry.find_pending(entity_id);
    if (pending == nullptr) {
        return invalid_state();
    }
    const auto revision = pending->entry.revision;
    const auto peer = pending->peer;
    ctx_.registry.remove_pending(entity_id);
    // 拒否された参加者にはストリップも接続状態も割り当てない。
    ctx_.deny(peer, ErrorCode::kRejected);
    ctx_.publish_pending();
    return revision;
}

HostCommandHandler::CommandResult HostCommandHandler::remove_strip(const std::string& entity_id) {
    auto* p = ctx_.registry.find(entity_id);
    if (p == nullptr) {
        return invalid_state();
    }
    // 音を出しうるストリップを消さないよう、削除できるのは接続が終わったものだけにする。
    if (p->strip.connection_state != ConnectionState::kLost &&
        p->strip.connection_state != ConnectionState::kFailed) {
        return invalid_state();
    }
    const auto revision = p->strip.revision;
    ctx_.ports.mixer.remove_channel(entity_id);
    ctx_.registry.remove(entity_id);
    ctx_.ports.ui.publish(cp::ChannelRemoved{entity_id});
    return revision;
}

// --- 連続値のコマンド ---

void HostCommandHandler::handle_gain(const cp::SetGain& command) {
    const auto gain = clamp_fader(command.gain_db);
    if (command.entity_id == kMasterEntityId) {
        if (last_master_gain_seq_ && command.seq < *last_master_gain_seq_) {
            return;
        }
        last_master_gain_seq_ = command.seq;
        ctx_.master.gain_db = gain;
        ctx_.settings.master_gain_db = gain;
        ctx_.ports.mixer.set_master_gain(gain);
        ctx_.publish_master();
        ctx_.ports.ui.publish(cp::GainAck{command.entity_id, command.seq, ctx_.master.revision});
        return;
    }
    auto* p = ctx_.registry.find(command.entity_id);
    if (p == nullptr) {
        // セッションや承認待ちにはフェーダーがない。SetGain には拒否の応答がないので捨てる。
        ctx_.log(common::LogLevel::kDebug, "フェーダーのない entity への SetGain を捨てました");
        return;
    }
    if (p->last_gain_seq && command.seq < *p->last_gain_seq) {
        return;
    }
    p->last_gain_seq = command.seq;
    p->strip.gain_db = gain;
    ctx_.ports.mixer.set_channel_gain(p->strip.entity_id, gain);
    ctx_.publish_channel(*p);
    ctx_.ports.ui.publish(cp::GainAck{command.entity_id, command.seq, p->strip.revision});
}

}  // namespace vocroz::session
