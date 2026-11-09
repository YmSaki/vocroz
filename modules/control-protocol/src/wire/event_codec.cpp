// SPDX-License-Identifier: AGPL-3.0-only
//
// エンジンから UI へのメッセージと JSON の変換の実装。
// 種別ごとのフィールドは write_fields() と read_fields() の組で書く。

#include "wire/event_codec.h"

#include <type_traits>

#include "wire/entity_codec.h"
#include "wire/field_rules.h"
#include "wire/json_values.h"
#include "wire/message_types.h"
#include "wire/object_reader.h"

namespace vocroz::control_protocol::wire {
namespace {

void write_fields(nlohmann::json& out, const MixerSnapshotEvent& event) {
    out["snapshot"] = write_json(event.snapshot);
}
void read_fields(ObjectReader& r, MixerSnapshotEvent& event) {
    event.snapshot = r.nested<MixerSnapshot>("snapshot", read_mixer_snapshot);
}

void write_fields(nlohmann::json& out, const ClientSnapshotEvent& event) {
    out["snapshot"] = write_json(event.snapshot);
}
void read_fields(ObjectReader& r, ClientSnapshotEvent& event) {
    event.snapshot = r.nested<ClientSnapshot>("snapshot", read_client_snapshot);
}

void write_fields(nlohmann::json& out, const SessionUpdated& event) {
    out["session"] = write_json(event.session);
}
void read_fields(ObjectReader& r, SessionUpdated& event) {
    event.session = r.nested<SessionInfo>("session", read_session_info);
}

void write_fields(nlohmann::json& out, const ChannelUpdated& event) {
    out["channel"] = write_json(event.channel);
}
void read_fields(ObjectReader& r, ChannelUpdated& event) {
    event.channel = r.nested<ChannelStrip>("channel", read_channel_strip);
}

void write_fields(nlohmann::json& out, const ChannelRemoved& event) {
    out["entity_id"] = event.entity_id;
}
void read_fields(ObjectReader& r, ChannelRemoved& event) {
    event.entity_id = r.text("entity_id", kIdentifierRule);
}

void write_fields(nlohmann::json& out, const MasterUpdated& event) {
    out["master"] = write_json(event.master);
}
void read_fields(ObjectReader& r, MasterUpdated& event) {
    event.master = r.nested<MasterStrip>("master", read_master_strip);
}

void write_fields(nlohmann::json& out, const PendingUpdated& event) {
    auto pending = nlohmann::json::array();
    for (const auto& participant : event.pending) {
        pending.push_back(write_json(participant));
    }
    out["pending"] = std::move(pending);
}
void read_fields(ObjectReader& r, PendingUpdated& event) {
    event.pending = r.list<PendingParticipant>("pending", read_pending_participant);
}

void write_fields(nlohmann::json& out, const ClientUpdated& event) {
    out["client"] = write_json(event.client);
}
void read_fields(ObjectReader& r, ClientUpdated& event) {
    event.client = r.nested<ClientSnapshot>("client", read_client_snapshot);
}

void write_fields(nlohmann::json& out, const RoutingUpdated& event) {
    out["routing"] = write_json(event.routing);
}
void read_fields(ObjectReader& r, RoutingUpdated& event) {
    event.routing = r.nested<RoutingInfo>("routing", read_routing_info);
}

void write_fields(nlohmann::json& out, const RouteLevelUpdated& event) {
    out["level"] = write_json(event.level);
}
void read_fields(ObjectReader& r, RouteLevelUpdated& event) {
    event.level = r.nested<RouteLevel>("level", read_route_level);
}

void write_fields(nlohmann::json& out, const Ack& event) {
    out["request_id"] = event.request_id;
    out["applied_revision"] = event.applied_revision;
}
void read_fields(ObjectReader& r, Ack& event) {
    event.request_id = r.text("request_id", kIdentifierRule);
    event.applied_revision = r.counter("applied_revision");
}

void write_fields(nlohmann::json& out, const Reject& event) {
    out["request_id"] = event.request_id;
    out["error_code"] = enum_json(event.error_code);
}
void read_fields(ObjectReader& r, Reject& event) {
    event.request_id = r.text("request_id", kIdentifierRule);
    event.error_code = r.enumeration<ErrorCode>("error_code");
}

void write_fields(nlohmann::json& out, const GainAck& event) {
    out["entity_id"] = event.entity_id;
    out["seq"] = event.seq;
    out["applied_revision"] = event.applied_revision;
}
void read_fields(ObjectReader& r, GainAck& event) {
    event.entity_id = r.text("entity_id", kIdentifierRule);
    event.seq = r.counter("seq");
    event.applied_revision = r.counter("applied_revision");
}

void write_fields(nlohmann::json& out, const Meters& event) {
    // UI が entity_id で引けるよう、配列ではなく entity_id をキーにしたオブジェクトで送る。
    auto levels = nlohmann::json::object();
    for (const auto& level : event.levels) {
        levels[level.entity_id] = nullable(level.peak_db);
    }
    out["levels"] = std::move(levels);
}
void read_fields(ObjectReader& r, Meters& event) {
    const nlohmann::json* levels = r.object("levels");
    if (levels == nullptr) {
        return;
    }
    event.levels.reserve(levels->size());
    // nlohmann/json のオブジェクトはキーの昇順に並ぶので、levels も entity_id の昇順になる。
    for (const auto& [entity_id, peak] : levels->items()) {
        if (!satisfies(entity_id, kIdentifierRule)) {
            r.fail(DecodeErrorCode::kOutOfRange, "levels");
            return;
        }
        if (!peak.is_null() && !peak.is_number()) {
            r.fail(DecodeErrorCode::kWrongType, "levels");
            return;
        }
        event.levels.push_back(MeterLevel{
            entity_id, peak.is_null() ? std::nullopt : std::optional<double>{peak.get<double>()}});
    }
}

void write_fields(nlohmann::json& out, const LinkStats& event) {
    // メーターと同じく、UI が entity_id で引けるようオブジェクトで送る。
    auto stats = nlohmann::json::object();
    for (const auto& stat : event.stats) {
        stats[stat.entity_id] = {{"jitter_ms", nullable(stat.jitter_ms)},
                                 {"buffer_ms", nullable(stat.buffer_ms)}};
    }
    out["stats"] = std::move(stats);
}
void read_fields(ObjectReader& r, LinkStats& event) {
    const nlohmann::json* stats = r.object("stats");
    if (stats == nullptr) {
        return;
    }
    event.stats.reserve(stats->size());
    // nlohmann/json のオブジェクトはキーの昇順に並ぶので、stats も entity_id の昇順になる。
    for (const auto& [entity_id, value] : stats->items()) {
        if (!satisfies(entity_id, kIdentifierRule)) {
            r.fail(DecodeErrorCode::kOutOfRange, "stats");
            return;
        }
        const auto read = [&](const char* key) -> std::optional<std::optional<double>> {
            const auto it = value.find(key);
            if (it == value.end() || (!it->is_null() && !it->is_number())) {
                return std::nullopt;
            }
            return it->is_null() ? std::optional<double>{}
                                 : std::optional<double>{it->get<double>()};
        };
        if (!value.is_object()) {
            r.fail(DecodeErrorCode::kWrongType, "stats");
            return;
        }
        const auto jitter = read("jitter_ms");
        const auto buffer = read("buffer_ms");
        if (!jitter || !buffer) {
            r.fail(DecodeErrorCode::kWrongType, "stats");
            return;
        }
        event.stats.push_back(LinkStat{entity_id, *jitter, *buffer});
    }
}

void write_fields(nlohmann::json& out, const Notice& event) {
    out["severity"] = enum_json(event.severity);
    out["code"] = event.code;
    out["params"] = event.params;
}
void read_fields(ObjectReader& r, Notice& event) {
    event.severity = r.enumeration<NoticeSeverity>("severity");
    event.code = r.text("code", kNoticeKeyRule);
    const nlohmann::json* params = r.object("params");
    if (params == nullptr) {
        return;
    }
    for (const auto& [name, value] : params->items()) {
        if (!value.is_string()) {
            r.fail(DecodeErrorCode::kWrongType, "params");
            return;
        }
        const auto& text = value.get_ref<const std::string&>();
        if (!satisfies(name, kNoticeKeyRule) || !satisfies(text, kNoticeValueRule)) {
            r.fail(DecodeErrorCode::kOutOfRange, "params");
            return;
        }
        event.params.emplace(name, text);
    }
}

}  // namespace

nlohmann::json write_engine_to_ui(const EngineToUiMessage& message) {
    auto out = nlohmann::json::object();
    std::visit(
        [&out](const auto& event) {
            out["type"] = type_name_of(event);
            write_fields(out, event);
        },
        message);
    return out;
}

common::Result<EngineToUiMessage, DecodeError> read_engine_to_ui(const nlohmann::json& value) {
    if (!value.is_object()) {
        return common::make_unexpected(DecodeError{DecodeErrorCode::kNotObject, {}});
    }
    ObjectReader r{value};
    const auto type = r.text("type", kTypeRule);
    if (!r.ok()) {
        return common::make_unexpected(r.finish().error());
    }
    auto message = read_alternative<EngineToUiMessage>(type, [&r](auto tag) {
        typename decltype(tag)::type event{};
        read_fields(r, event);
        return event;
    });
    if (!message) {
        return common::make_unexpected(DecodeError{DecodeErrorCode::kUnknownType, "type"});
    }
    if (auto finished = r.finish(); !finished) {
        return common::make_unexpected(finished.error());
    }
    return std::move(*message);
}

}  // namespace vocroz::control_protocol::wire
