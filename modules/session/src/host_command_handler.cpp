// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の UI から届いたコマンドの適用の実装。
// 離散コマンドは「重複 → 対象の有無（NOT_FOUND）→ revision（REVISION_CONFLICT）→
// 種類と状態（INVALID_STATE）」の順に検査する。

#include "host_command_handler.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "vocroz/session/entity_ids.h"
#include "vocroz/session/ports/secret_store.h"
#include "vocroz/transport/srtp_master_key.h"

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
                // 前の接続から抜き差しされたデバイスがあるかもしれないので、列挙し直してから送る。
                ctx_.refresh_output_devices();
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
    if (entity_id == kRoutingEntityId) {
        return ctx_.routing.info().revision;
    }
    if (entity_id == kExternalOutputEntityId) {
        return ctx_.external_output.info().revision;
    }
    if (const auto* level = ctx_.routing.find_level(entity_id)) {
        return level->revision;
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
            } else if constexpr (std::is_same_v<B, cp::StageRouting>) {
                return id == kRoutingEntityId ? stage_routing(body.config) : invalid_state();
            } else if constexpr (std::is_same_v<B, cp::CommitRouting>) {
                return id == kRoutingEntityId ? commit_routing(body.approve_feedback)
                                              : invalid_state();
            } else if constexpr (std::is_same_v<B, cp::DiscardRouting>) {
                return id == kRoutingEntityId ? discard_routing() : invalid_state();
            } else if constexpr (std::is_same_v<B, cp::SetRouteMute>) {
                return set_route_mute(id, body.muted);
            } else if constexpr (std::is_same_v<B, cp::SelectOutput>) {
                return id == kSessionEntityId ? select_output(body.device_id) : invalid_state();
            } else if constexpr (std::is_same_v<B, cp::SetReturnMute>) {
                return set_return_mute(id, body.muted);
            } else if constexpr (std::is_same_v<B, cp::SetLatencyTarget>) {
                return id == kSessionEntityId ? set_latency_target(body.target_ms)
                                              : invalid_state();
            } else if constexpr (std::is_same_v<B, cp::SetExternalOutputConfig>) {
                return id == kExternalOutputEntityId ? set_external_output_config(body.config)
                                                     : invalid_state();
            } else if constexpr (std::is_same_v<B, cp::SetExternalOutputKey>) {
                return id == kExternalOutputEntityId ? set_external_output_key(body.key)
                                                     : invalid_state();
            } else if constexpr (std::is_same_v<B, cp::SetExternalOutputRunning>) {
                return id == kExternalOutputEntityId ? set_external_output_running(body.running)
                                                     : invalid_state();
            } else if constexpr (std::is_same_v<B, cp::TestExternalOutput>) {
                return id == kExternalOutputEntityId ? test_external_output() : invalid_state();
            } else {
                static_assert(
                    std::is_same_v<B, cp::Join> || std::is_same_v<B, cp::Leave> ||
                    std::is_same_v<B, cp::SelectInput> || std::is_same_v<B, cp::SetSendMute> ||
                    std::is_same_v<B, cp::SetInputChannels> || std::is_same_v<B, cp::SetAppAudio>);
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
    // 子機の入力ごとにストリップを作るので、すべての入力のぶんの空きが要る。
    if (ctx_.registry.participants().size() + pending->streams.size() > kMaxChannelStrips) {
        return invalid_state();
    }
    const auto revision = pending->entry.revision;
    const auto peer = pending->peer;
    const auto token = detail::random_bytes<std::tuple_size_v<ReconnectToken>>(ctx_.ports.random);
    const std::string participant_id =
        ctx_.registry.add_participant(*pending, token).strip.participant_id;
    ctx_.registry.remove_pending(entity_id);
    const auto strips = ctx_.registry.strips_of(participant_id);
    for (const Participant* strip : strips) {
        ctx_.ports.mixer.add_channel(strip->strip.entity_id);
    }
    ctx_.admit_streams(peer, participant_id);
    ctx_.sync_return(*strips.front());
    ctx_.send(peer, wire::JoinApproved{token});
    ctx_.publish_pending();
    for (const Participant* strip : strips) {
        ctx_.publish_channel(*strip);
    }
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
    // 同じ子機の入力のストリップは接続状態と再接続トークンを共有するので、まとめて削除する。
    // どれも接続が終わっているので、音を出しうるストリップは消さない。
    const auto revision = p->strip.revision;
    const std::string participant_id = p->strip.participant_id;
    ctx_.remove_participant_strips(participant_id);
    return revision;
}

HostCommandHandler::CommandResult HostCommandHandler::set_return_mute(const std::string& entity_id,
                                                                      bool muted) {
    auto* p = ctx_.registry.find(entity_id);
    if (p == nullptr) {
        return invalid_state();
    }
    if (p->strip.return_muted != muted) {
        // 返送は子機 1 台に 1 本なので、同じ子機のストリップはすべて同じ返送の状態を持つ。
        // どのストリップから操作しても、その子機のすべてのストリップに反映する。
        // 解除のたびに世代番号を進め、ミュート前に送ったフレームと解除後のフレームを子機が
        // 区別できるようにする（制御メッセージとメディアの到着順は逆転しうる）。
        const std::uint32_t epoch = muted ? p->return_epoch : p->return_epoch + 1;
        const auto strips = ctx_.registry.strips_of(p->strip.participant_id);
        for (Participant* strip : strips) {
            strip->return_epoch = epoch;
            strip->strip.return_muted = muted;
            detail::bump_revision(strip->strip.revision);
        }
        ctx_.sync_return(*p);
        for (const Participant* strip : strips) {
            ctx_.publish_channel(*strip);
        }
    }
    return p->strip.revision;
}

// --- ルーティング ---

HostCommandHandler::CommandResult HostCommandHandler::stage_routing(
    const cp::RoutingConfig& config) {
    // ステージングは検証の結果を示すための段階なので、問題のある構成も受け付けて理由を返す。
    ctx_.routing.stage(config, ctx_.strip_ids());
    ctx_.publish_routing();
    return ctx_.routing.info().revision;
}

HostCommandHandler::CommandResult HostCommandHandler::commit_routing(bool approve_feedback) {
    if (auto committed = ctx_.commit_staged_routing(approve_feedback); !committed) {
        // 検証し直した結果が変わっていることがあるので、拒否の理由を UI が示せるよう知らせる。
        ctx_.publish_routing();
        return common::make_unexpected(committed.error());
    }
    // 消えたバスとセンドの seq は、同じ entity_id が作り直されたときに古い値と比べないよう忘れる。
    std::erase_if(last_route_gain_seq_, [this](const auto& entry) {
        return ctx_.routing.find_level(entry.first) == nullptr;
    });
    ctx_.publish_routing();
    // 本番中に落ちても、次の起動で同じバスと出力の構成に戻れるよう、適用のたびに保存する。
    ctx_.save_settings();
    return ctx_.routing.info().revision;
}

HostCommandHandler::CommandResult HostCommandHandler::discard_routing() {
    if (auto discarded = ctx_.routing.discard(); !discarded) {
        return common::make_unexpected(discarded.error());
    }
    ctx_.publish_routing();
    return ctx_.routing.info().revision;
}

HostCommandHandler::CommandResult HostCommandHandler::set_route_mute(const std::string& entity_id,
                                                                     bool muted) {
    auto* level = ctx_.routing.find_level(entity_id);
    if (level == nullptr) {
        return invalid_state();
    }
    if (level->muted != muted) {
        level->muted = muted;
        ctx_.ports.mixer.set_route_muted(entity_id, muted);
        detail::bump_revision(level->revision);
        ctx_.publish_route_level(*level);
    }
    return level->revision;
}

// --- 目標レイテンシー ---

HostCommandHandler::CommandResult HostCommandHandler::set_latency_target(double target_ms) {
    // 範囲は control-protocol の信頼境界で検証済み。同じ値なら何も変えない。
    if (target_ms != ctx_.session.latency_target_ms) {
        ctx_.apply_latency_target(target_ms);
        detail::bump_revision(ctx_.session.revision);
        ctx_.publish_session();
        ctx_.save_settings();
    }
    return ctx_.session.revision;
}

// --- 出力デバイス ---

HostCommandHandler::CommandResult HostCommandHandler::select_output(
    const std::optional<std::string>& device_id) {
    ctx_.refresh_output_devices();
    const bool already_open = ctx_.session.output_device_state == cp::OutputDeviceState::kOpen &&
                              ctx_.session.output_device_id == device_id;
    const bool already_none =
        !device_id && ctx_.session.output_device_state == cp::OutputDeviceState::kNone;
    if (already_open || already_none) {
        // 開き直すと音が途切れるので何もしない。一覧だけは新しくなっているので知らせる。
        ctx_.publish_session();
        return ctx_.session.revision;
    }
    bool opened = true;
    if (device_id) {
        opened = ctx_.open_output(*device_id);
    } else {
        ctx_.close_output();
        ctx_.settings.output_device_id.reset();
        ctx_.save_settings();
    }
    // 開けなかったときも、前のデバイスは閉じて状態が変わっているので revision を進めて知らせる。
    // Master のミュートは変えない（音を出すかはオペレーターが Master で決める）。
    detail::bump_revision(ctx_.session.revision);
    ctx_.publish_session();
    if (!opened) {
        ctx_.log(common::LogLevel::kWarning, "選択した出力デバイスを開けませんでした");
        return common::make_unexpected(ErrorCode::kDeviceUnavailable);
    }
    return ctx_.session.revision;
}

// --- SRTP 外部出力 ---

HostCommandHandler::CommandResult HostCommandHandler::set_external_output_config(
    const cp::ExternalOutputConfig& config) {
    if (!ctx_.is_external_output_source(config.source_id)) {
        return common::make_unexpected(ErrorCode::kNotFound);
    }
    const auto before = ctx_.external_output.info().revision;
    if (auto set = ctx_.external_output.set_config(config); !set) {
        return common::make_unexpected(set.error());
    }
    if (ctx_.external_output.info().revision != before) {
        ctx_.settings.external_output = config;
        ctx_.save_settings();
        ctx_.publish_external_output();
    }
    return ctx_.external_output.info().revision;
}

HostCommandHandler::CommandResult HostCommandHandler::set_external_output_key(
    const std::string& text) {
    // 鍵の形式の検証は transport の信頼境界（parse_srtp_master_key()）に任せる。
    auto key = transport::parse_srtp_master_key(text);
    if (!key) {
        return invalid_state();
    }
    if (auto set = ctx_.external_output.set_key(key.value()); !set) {
        return common::make_unexpected(set.error());
    }
    // 保存するのは受信機に貼り付けるのと同じ形式の文字列。読み込みでも同じパーサーで検証する。
    const std::string formatted = transport::format_srtp_master_key(key.value());
    const std::vector<std::uint8_t> bytes(formatted.begin(), formatted.end());
    if (!ctx_.ports.secrets.save(SecretSlot::kExternalOutputKey, bytes)) {
        // 保存できなくても今回の起動では使える。次の起動では未設定に戻るので記録だけ残す。
        ctx_.log(common::LogLevel::kWarning, "外部出力の鍵を保存できませんでした");
    }
    ctx_.publish_external_output();
    return ctx_.external_output.info().revision;
}

HostCommandHandler::CommandResult HostCommandHandler::set_external_output_running(bool running) {
    auto& book = ctx_.external_output;
    const auto state = book.info().state;
    if (!running) {
        if (state != cp::ExternalOutputState::kStopped) {
            ctx_.ports.external_output.stop();
            book.record_stop();
            ctx_.publish_external_output();
        }
        return book.info().revision;
    }
    if (state == cp::ExternalOutputState::kRunning) {
        return book.info().revision;
    }
    if (state == cp::ExternalOutputState::kTesting ||
        !ctx_.is_external_output_source(book.info().config.source_id)) {
        return invalid_state();
    }
    // 宛先か鍵がなければ始めない（平文では送らない）。
    const auto plan = book.plan();
    if (!plan) {
        return common::make_unexpected(plan.error());
    }
    const auto started = ctx_.ports.external_output.start(plan.value(), *book.key());
    book.record_start(started);
    ctx_.publish_external_output();
    if (!started) {
        ctx_.notice(cp::NoticeSeverity::kWarning, "external_output_failed", {});
    }
    return book.info().revision;
}

HostCommandHandler::CommandResult HostCommandHandler::test_external_output() {
    auto& book = ctx_.external_output;
    // 接続テストは止めている間だけ行う。送信中に試験音を混ぜない。
    if (book.info().state != cp::ExternalOutputState::kStopped) {
        return invalid_state();
    }
    const auto plan = book.plan();
    if (!plan) {
        return common::make_unexpected(plan.error());
    }
    book.record_test_start(ctx_.ports.external_output.start_test(plan.value(), *book.key()));
    ctx_.publish_external_output();
    return book.info().revision;
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
    if (auto* level = ctx_.routing.find_level(command.entity_id)) {
        handle_route_gain(*level, command);
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

void HostCommandHandler::handle_route_gain(cp::RouteLevel& level, const cp::SetGain& command) {
    const auto last = last_route_gain_seq_.find(level.entity_id);
    if (last != last_route_gain_seq_.end() && command.seq < last->second) {
        return;
    }
    last_route_gain_seq_[level.entity_id] = command.seq;
    // バスとセンドのフェーダーも、チャンネルと同じ範囲（-∞〜+10 dB）にする。
    level.gain_db = clamp_fader(command.gain_db);
    ctx_.ports.mixer.set_route_gain(level.entity_id, level.gain_db);
    ctx_.publish_route_level(level);
    ctx_.ports.ui.publish(cp::GainAck{command.entity_id, command.seq, level.revision});
}

}  // namespace vocroz::session
