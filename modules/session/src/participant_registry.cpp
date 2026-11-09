// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の参加者の一覧の実装。

#include "participant_registry.h"

#include <algorithm>
#include <string>

#include "detail/token_utils.h"

namespace vocroz::session {
namespace {

// 短い識別 ID（例: "#08"）。2 桁にそろえ、100 人目からは桁を増やす。
std::string make_short_id(std::uint64_t number) {
    auto digits = std::to_string(number);
    if (digits.size() < 2) {
        digits.insert(0, "0");
    }
    return "#" + digits;
}

// 入力の申告をストリップの値に写す。番号は renumber() で振る。
void apply_stream(Participant& p, const InputStream& stream) {
    p.stream_id = stream.stream_id;
    p.strip.source_kind = stream.kind;
    p.strip.channel_count = stream.channels;
}

}  // namespace

PendingJoin& ParticipantRegistry::add_pending(PeerHandle peer, std::string name) {
    PendingJoin pending;
    pending.entry.entity_id = "pending" + std::to_string(next_pending_number_++);
    // 承認待ちの entity にはコマンドで変える値がないので、revision は 1 のまま進まない。
    pending.entry.revision = 1;
    pending.entry.name = std::move(name);
    pending.entry.short_id = make_short_id(next_short_id_++);
    pending.peer = peer;
    return pending_.emplace_back(std::move(pending));
}

PendingJoin* ParticipantRegistry::find_pending(std::string_view entity_id) noexcept {
    const auto it = std::ranges::find_if(
        pending_, [&](const PendingJoin& p) { return p.entry.entity_id == entity_id; });
    return it == pending_.end() ? nullptr : &*it;
}

PendingJoin* ParticipantRegistry::find_pending_by_peer(PeerHandle peer) noexcept {
    const auto it =
        std::ranges::find_if(pending_, [&](const PendingJoin& p) { return p.peer == peer; });
    return it == pending_.end() ? nullptr : &*it;
}

void ParticipantRegistry::remove_pending(std::string_view entity_id) {
    std::erase_if(pending_, [&](const PendingJoin& p) { return p.entry.entity_id == entity_id; });
}

std::vector<control_protocol::PendingParticipant> ParticipantRegistry::pending_entries() const {
    std::vector<control_protocol::PendingParticipant> entries;
    entries.reserve(pending_.size());
    for (const auto& p : pending_) {
        entries.push_back(p.entry);
    }
    return entries;
}

Participant& ParticipantRegistry::add_participant(const PendingJoin& pending,
                                                  const ReconnectToken& token) {
    const auto number = std::to_string(next_strip_number_++);
    Participant participant;
    participant.strip.entity_id = "ch" + number;
    participant.strip.revision = 1;
    participant.strip.participant_id = "p" + number;
    participant.strip.name = pending.entry.name;
    participant.strip.short_id = pending.entry.short_id;
    participant.strip.connection_state = control_protocol::ConnectionState::kConnecting;
    participant.strip.signal_state = std::nullopt;
    // 初期ミュート: 新しい参加者はチャンネルミュートで始める。子機も送信ミュートで参加する。
    participant.strip.send_muted = true;
    participant.strip.channel_muted = true;
    // 返送モニターも OFF（ミュート）で始める。
    participant.strip.return_muted = true;
    participant.strip.gain_db = 0.0;
    participant.reconnect_token = token;
    participant.peer = pending.peer;
    const std::string participant_id = participant.strip.participant_id;
    apply_stream(participant, pending.streams.front());
    const std::size_t first = participants_.size();
    participants_.push_back(std::move(participant));
    for (std::size_t i = 1; i < pending.streams.size(); ++i) {
        static_cast<void>(add_stream(participant_id, pending.streams[i]));
    }
    static_cast<void>(renumber(participant_id));
    return participants_[first];
}

Participant& ParticipantRegistry::add_stream(std::string_view participant_id,
                                             const InputStream& stream) {
    const auto siblings = strips_of(participant_id);
    Participant strip = *siblings.front();
    strip.strip.entity_id = "ch" + std::to_string(next_strip_number_++);
    strip.strip.revision = 1;
    // 初期ミュート: 後から足した入力のストリップもチャンネルミュートで始める。
    strip.strip.channel_muted = true;
    strip.strip.gain_db = 0.0;
    strip.last_gain_seq.reset();
    apply_stream(strip, stream);
    return participants_.emplace_back(std::move(strip));
}

std::vector<Participant*> ParticipantRegistry::strips_of(std::string_view participant_id) {
    std::vector<Participant*> strips;
    for (auto& p : participants_) {
        if (p.strip.participant_id == participant_id) {
            strips.push_back(&p);
        }
    }
    return strips;
}

std::vector<Participant*> ParticipantRegistry::renumber(std::string_view participant_id) {
    auto strips = strips_of(participant_id);
    std::vector<Participant*> by_stream = strips;
    std::ranges::sort(by_stream, {}, [](const Participant* p) { return p->stream_id; });
    std::vector<Participant*> changed;
    std::uint8_t microphones = 0;
    std::uint8_t application_audio = 0;
    for (Participant* p : by_stream) {
        std::uint8_t& count = p->strip.source_kind == control_protocol::InputSourceKind::kMicrophone
                                  ? microphones
                                  : application_audio;
        ++count;
        if (p->strip.source_number != count) {
            p->strip.source_number = count;
            changed.push_back(p);
        }
    }
    return changed;
}

Participant* ParticipantRegistry::find(std::string_view entity_id) noexcept {
    const auto it = std::ranges::find_if(
        participants_, [&](const Participant& p) { return p.strip.entity_id == entity_id; });
    return it == participants_.end() ? nullptr : &*it;
}

Participant* ParticipantRegistry::find_by_peer(PeerHandle peer) noexcept {
    const auto it =
        std::ranges::find_if(participants_, [&](const Participant& p) { return p.peer == peer; });
    return it == participants_.end() ? nullptr : &*it;
}

Participant* ParticipantRegistry::find_by_token(const ReconnectToken& token) noexcept {
    // 一致を見つけても打ち切らず、全員と比べる。照合の時間から何人目と一致したかを漏らさないため。
    Participant* found = nullptr;
    for (auto& p : participants_) {
        if (detail::constant_time_equal(p.reconnect_token, token)) {
            found = &p;
        }
    }
    return found;
}

void ParticipantRegistry::remove(std::string_view entity_id) {
    std::erase_if(participants_,
                  [&](const Participant& p) { return p.strip.entity_id == entity_id; });
}

void ParticipantRegistry::remove_participant(std::string_view participant_id) {
    std::erase_if(participants_,
                  [&](const Participant& p) { return p.strip.participant_id == participant_id; });
}

bool ParticipantRegistry::any_active() const noexcept {
    return std::ranges::any_of(participants_, [](const Participant& p) {
        return p.strip.connection_state == control_protocol::ConnectionState::kConnected ||
               p.strip.connection_state == control_protocol::ConnectionState::kReconnecting;
    });
}

void ParticipantRegistry::clear() noexcept {
    pending_.clear();
    participants_.clear();
}

}  // namespace vocroz::session
