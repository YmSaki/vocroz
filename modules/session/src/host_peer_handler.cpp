// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機からの制御メッセージと、接続の状態変化の処理の実装。
// 子機のメッセージは信頼境界（wire/）でデコードし、検証済みの型だけをここで扱う。
// 子機から親機を操作するコマンドはワイヤーフォーマットに存在しないので、デコードの段階で捨てられる。

#include "host_peer_handler.h"

#include <algorithm>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "detail/text_utils.h"
#include "detail/token_utils.h"
#include "wire/peer_message_codec.h"

namespace vocroz::session {
namespace {

namespace cp = control_protocol;
using cp::ConnectionState;
using cp::ErrorCode;
using state::ConnectionEvent;

}  // namespace

HostPeerHandler::HostPeerHandler(HostContext& context) : ctx_(context) {}

void HostPeerHandler::on_peer_message(PeerHandle peer, std::span<const std::uint8_t> bytes) {
    auto decoded = wire::decode_client_to_host(bytes);
    if (!decoded) {
        // 子機は承認前でもメッセージを送れるので、不正なメッセージは捨てるだけにする。
        ctx_.log(common::LogLevel::kWarning, "子機からの不正な制御メッセージを捨てました");
        return;
    }
    std::visit(
        [this, peer](const auto& m) {
            using M = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<M, wire::JoinRequest>) {
                handle_join_request(peer, m);
            } else if constexpr (std::is_same_v<M, wire::ReconnectRequest>) {
                handle_reconnect(peer, m.reconnect_token);
            } else if constexpr (std::is_same_v<M, wire::SendMuteReport>) {
                handle_send_mute(peer, m);
            } else if constexpr (std::is_same_v<M, wire::InputReport>) {
                handle_input_report(peer, m);
            } else if constexpr (std::is_same_v<M, wire::StreamsReport>) {
                handle_streams_report(peer, m);
            } else {
                static_assert(std::is_same_v<M, wire::LeaveNotice>);
                handle_peer_left(peer);
            }
        },
        decoded.value());
}

void HostPeerHandler::on_peer_established(PeerHandle peer) {
    if (auto* p = ctx_.registry.find_by_peer(peer)) {
        ctx_.advance_participant(*p, ConnectionEvent::kEstablished);
    }
}

void HostPeerHandler::on_peer_path_failed(PeerHandle peer) {
    if (auto* p = ctx_.registry.find_by_peer(peer)) {
        if (ctx_.advance_participant(*p, ConnectionEvent::kPathFailed)) {
            ctx_.release_peer(*p);
        }
    }
}

void HostPeerHandler::on_peer_timeout(PeerHandle peer) {
    if (const auto* pending = ctx_.registry.find_pending_by_peer(peer)) {
        // 承認待ちのまま回線が切れた。ストリップはまだないので、申請ごと取り下げる。
        ctx_.registry.remove_pending(pending->entry.entity_id);
        ctx_.ports.peers.close(peer);
        ctx_.publish_pending();
        return;
    }
    if (auto* p = ctx_.registry.find_by_peer(peer)) {
        if (ctx_.advance_participant(*p, ConnectionEvent::kReceiveTimeout)) {
            ctx_.notice(cp::NoticeSeverity::kWarning, "participant_reconnecting", label(p->strip));
        }
    }
}

void HostPeerHandler::on_peer_resumed(PeerHandle peer) {
    if (auto* p = ctx_.registry.find_by_peer(peer)) {
        ctx_.advance_participant(*p, ConnectionEvent::kResumed);
    }
}

void HostPeerHandler::on_peer_closed(PeerHandle peer) {
    if (const auto* pending = ctx_.registry.find_pending_by_peer(peer)) {
        ctx_.registry.remove_pending(pending->entry.entity_id);
        ctx_.publish_pending();
        return;
    }
    handle_peer_left(peer);
}

void HostPeerHandler::on_signal_state(PeerHandle peer, cp::SignalState signal) {
    const auto* first = ctx_.registry.find_by_peer(peer);
    if (first == nullptr) {
        return;
    }
    // 信号状態は接続（子機）ごとの判定なので、同じ子機のストリップにそろえて写す。
    for (Participant* p : ctx_.registry.strips_of(first->strip.participant_id)) {
        if (p->peer != peer || p->strip.connection_state != ConnectionState::kConnected ||
            p->strip.signal_state == signal) {
            continue;
        }
        p->strip.signal_state = signal;
        ctx_.publish_channel(*p);
    }
}

// --- 子機からの制御メッセージ ---

void HostPeerHandler::handle_join_request(PeerHandle peer, const wire::JoinRequest& request) {
    if (ctx_.registry.find_pending_by_peer(peer) != nullptr ||
        ctx_.registry.find_by_peer(peer) != nullptr) {
        // 同じ接続からの二重の申請。最初の申請の扱いを変えない。
        return;
    }
    if (!ctx_.accepting_joins()) {
        ctx_.deny(peer, ErrorCode::kSessionEnded);
        return;
    }
    if (request.reconnect_token) {
        if (auto* p = ctx_.registry.find_by_token(*request.reconnect_token)) {
            rejoin(*p, peer);
            return;
        }
        // ストリップが削除されてトークンが無効なら、招待による新しい申請として扱う。
    }
    const auto verified = ctx_.invites.verify(request.invite_token, ctx_.ports.clock.now());
    if (!verified) {
        ctx_.deny(peer, verified.error());
        return;
    }
    if (ctx_.registry.pending_count() >= kMaxPendingParticipants) {
        ctx_.deny(peer, ErrorCode::kRejected);
        return;
    }
    const auto& pending = ctx_.registry.add_pending(peer, request.display_name);
    ctx_.send(peer, wire::JoinPending{ctx_.session.name});
    ctx_.publish_pending();
    ctx_.notice(cp::NoticeSeverity::kInfo, "join_request",
                pending.entry.name.empty() ? pending.entry.short_id : pending.entry.name);
}

void HostPeerHandler::handle_reconnect(PeerHandle peer, const ReconnectToken& token) {
    if (ctx_.registry.find_by_peer(peer) != nullptr) {
        return;
    }
    if (!ctx_.accepting_joins()) {
        ctx_.deny(peer, ErrorCode::kSessionEnded);
        return;
    }
    auto* p = ctx_.registry.find_by_token(token);
    if (p == nullptr) {
        // ストリップが削除されたか、知らないトークン。再接続の猶予切れとして伝える。
        ctx_.deny(peer, ErrorCode::kReconnectTimeout);
        return;
    }
    rejoin(*p, peer);
}

// 再接続トークンを持つ子機を、承認なしで元のストリップへ戻す（新しいストリップは作らない）。
// 入力が変わっていれば、続けて届く入力の申告でストリップをそろえる。
void HostPeerHandler::rejoin(Participant& p, PeerHandle peer) {
    if (p.peer && *p.peer != peer) {
        ctx_.ports.peers.close(*p.peer);
    }
    const std::string participant_id = p.strip.participant_id;
    for (Participant* strip : ctx_.registry.strips_of(participant_id)) {
        strip->peer = peer;
    }
    ctx_.advance_participant(p, ConnectionEvent::kReconnectStarted);
    ctx_.admit_streams(peer, participant_id);
    ctx_.send(peer, wire::JoinApproved{p.reconnect_token});
}

void HostPeerHandler::handle_send_mute(PeerHandle peer, const wire::SendMuteReport& report) {
    auto* p = ctx_.registry.find_by_peer(peer);
    if (p == nullptr) {
        return;
    }
    // 子機の送信ミュートは観測値なので revision を進めない。親機のチャンネルミュートには
    // 影響しない（親機がミュートしたチャンネルを子機から解除する手段はない）。
    // 送信ミュートは子機のすべての入力に掛かるので、同じ子機のストリップにそろえて写す。
    ctx_.ports.peers.apply_send_mute(peer, report.send_muted, report.epoch);
    for (Participant* strip : ctx_.registry.strips_of(p->strip.participant_id)) {
        if (strip->strip.send_muted != report.send_muted) {
            strip->strip.send_muted = report.send_muted;
            ctx_.publish_channel(*strip);
        }
    }
}

void HostPeerHandler::handle_input_report(PeerHandle peer, const wire::InputReport& report) {
    if (const auto* first = ctx_.registry.find_by_peer(peer)) {
        // ストリップの DTO にはまだ入力なしの欄がないので、状態だけを保持する。
        for (Participant* p : ctx_.registry.strips_of(first->strip.participant_id)) {
            p->input_missing = report.input_missing;
        }
    }
}

// 子機の入力の申告に、ストリップをそろえる。
// 変わらない入力（ストリーム ID、種類、チャンネル数が同じ）のストリップはそのまま残し、
// フェーダーとミュートとルーティングを保つ。種類かチャンネル数が変わった入力は、別の入力として
// ストリップを作り直す（チャンネル数の違うストリップでは、ルーティングとミキサーの扱いが変わるため）。
// 新しいストリップはチャンネルミュートで始める（初期ミュート）。
void HostPeerHandler::handle_streams_report(PeerHandle peer, const wire::StreamsReport& report) {
    if (auto* pending = ctx_.registry.find_pending_by_peer(peer)) {
        // 承認前は申告を覚えるだけで、ストリップは承認のときに作る。
        pending->streams = report.streams;
        return;
    }
    const auto* first = ctx_.registry.find_by_peer(peer);
    if (first == nullptr) {
        return;
    }
    const std::string participant_id = first->strip.participant_id;
    std::vector<std::string> obsolete;
    std::vector<InputStream> added;
    const auto strips = ctx_.registry.strips_of(participant_id);
    for (const Participant* strip : strips) {
        const bool declared = std::ranges::any_of(report.streams, [&](const InputStream& s) {
            return s.stream_id == strip->stream_id && s.kind == strip->strip.source_kind &&
                   s.channels == strip->strip.channel_count;
        });
        if (!declared) {
            obsolete.push_back(strip->strip.entity_id);
        }
    }
    for (const InputStream& stream : report.streams) {
        const bool exists = std::ranges::any_of(strips, [&](const Participant* strip) {
            return strip->stream_id == stream.stream_id &&
                   strip->strip.source_kind == stream.kind &&
                   strip->strip.channel_count == stream.channels;
        });
        if (!exists) {
            added.push_back(stream);
        }
    }
    // 新しいストリップは、同じ参加者のストリップから名前と接続状態を写すので、古いものを消す前に作る。
    std::size_t total = ctx_.registry.participants().size() - obsolete.size();
    std::vector<std::string> created;
    for (const InputStream& stream : added) {
        if (total >= kMaxChannelStrips) {
            // 届いたストリームは対応がないので受信側で捨てられる（音の経路を増やさない側に倒す）。
            ctx_.log(common::LogLevel::kWarning,
                     "ストリップの上限に達したため、子機の入力の一部を受け付けませんでした");
            break;
        }
        created.push_back(ctx_.registry.add_stream(participant_id, stream).strip.entity_id);
        ++total;
    }
    for (const auto& id : obsolete) {
        ctx_.remove_strip(id);
    }
    for (const auto& id : created) {
        ctx_.ports.mixer.add_channel(id);
    }
    const auto renumbered = ctx_.registry.renumber(participant_id);
    for (const auto& id : created) {
        if (const auto* strip = ctx_.registry.find(id)) {
            ctx_.publish_channel(*strip);
        }
    }
    for (const Participant* strip : renumbered) {
        if (std::ranges::find(created, strip->strip.entity_id) == created.end()) {
            ctx_.publish_channel(*strip);
        }
    }
    ctx_.admit_streams(peer, participant_id);
}

void HostPeerHandler::handle_peer_left(PeerHandle peer) {
    auto* p = ctx_.registry.find_by_peer(peer);
    if (p == nullptr) {
        return;
    }
    if (ctx_.advance_participant(*p, ConnectionEvent::kPeerLeft)) {
        ctx_.notice(cp::NoticeSeverity::kWarning, "participant_lost", label(p->strip));
    }
    ctx_.release_peer(*p);
}

}  // namespace vocroz::session
