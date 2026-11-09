// SPDX-License-Identifier: AGPL-3.0-only
//
// UI からエンジンへのメッセージと JSON の変換の実装。
// 種別ごとのフィールドは write_fields() と read_fields()
// の組で書き、フィールドのないコマンドは何もしない。

#include "wire/command_codec.h"

#include <type_traits>

#include "wire/entity_codec.h"
#include "wire/field_rules.h"
#include "wire/json_values.h"
#include "wire/message_types.h"
#include "wire/object_reader.h"

namespace vocroz::control_protocol::wire {
namespace {

void write_fields(nlohmann::json& out, const SetChannelMute& command) {
    out["muted"] = command.muted;
}
void read_fields(ObjectReader& r, SetChannelMute& command) {
    command.muted = r.boolean("muted");
}

void write_fields(nlohmann::json& out, const SetMasterMute& command) {
    out["muted"] = command.muted;
}
void read_fields(ObjectReader& r, SetMasterMute& command) {
    command.muted = r.boolean("muted");
}

void write_fields(nlohmann::json& out, const Join& command) {
    out["invite"] = command.invite;
}
void read_fields(ObjectReader& r, Join& command) {
    command.invite = r.text("invite", kInviteRule);
}

void write_fields(nlohmann::json& out, const SelectInput& command) {
    out["device_id"] = nullable(command.device_id);
}
void read_fields(ObjectReader& r, SelectInput& command) {
    command.device_id = r.nullable_text("device_id", kDeviceIdRule);
}

void write_fields(nlohmann::json& out, const SetSendMute& command) {
    out["muted"] = command.muted;
}
void read_fields(ObjectReader& r, SetSendMute& command) {
    command.muted = r.boolean("muted");
}

void write_fields(nlohmann::json& out, const StageRouting& command) {
    out["config"] = write_json(command.config);
}
void read_fields(ObjectReader& r, StageRouting& command) {
    command.config = r.nested<RoutingConfig>("config", read_routing_config);
}

void write_fields(nlohmann::json& out, const CommitRouting& command) {
    out["approve_feedback"] = command.approve_feedback;
}
void read_fields(ObjectReader& r, CommitRouting& command) {
    command.approve_feedback = r.boolean("approve_feedback");
}

void write_fields(nlohmann::json& out, const SetRouteMute& command) {
    out["muted"] = command.muted;
}
void read_fields(ObjectReader& r, SetRouteMute& command) {
    command.muted = r.boolean("muted");
}

void write_fields(nlohmann::json& out, const SetGain& command) {
    out["entity_id"] = command.entity_id;
    out["seq"] = command.seq;
    out["gain_db"] = nullable(command.gain_db);
}
void read_fields(ObjectReader& r, SetGain& command) {
    command.entity_id = r.text("entity_id", kIdentifierRule);
    command.seq = r.counter("seq");
    command.gain_db = r.gain("gain_db");
}

void write_fields(nlohmann::json& out, const DiscreteCommand& command) {
    out["request_id"] = command.request_id;
    out["entity_id"] = command.entity_id;
    out["expected_revision"] = command.expected_revision;
    std::visit(
        [&out](const auto& body) {
            out["type"] = type_name_of(body);
            if constexpr (!std::is_empty_v<std::decay_t<decltype(body)>>) {
                write_fields(out, body);
            }
        },
        command.body);
}

/// 離散コマンド以外の、UI からエンジンへのメッセージ。
using OtherMessage = std::variant<Hello, SetGain>;

/// 種別が T のメッセージのフィールドを読む。
template <typename T>
T read_body(ObjectReader& r) {
    T value{};
    if constexpr (!std::is_empty_v<T>) {
        read_fields(r, value);
    }
    return value;
}

}  // namespace

nlohmann::json write_ui_to_engine(const UiToEngineMessage& message) {
    auto out = nlohmann::json::object();
    std::visit(
        [&out](const auto& alternative) {
            using T = std::decay_t<decltype(alternative)>;
            if constexpr (std::is_same_v<T, DiscreteCommand>) {
                // 離散コマンドの type は本体の種別で決まる。
                write_fields(out, alternative);
            } else {
                out["type"] = type_name_of(alternative);
                if constexpr (!std::is_empty_v<T>) {
                    write_fields(out, alternative);
                }
            }
        },
        message);
    return out;
}

common::Result<UiToEngineMessage, DecodeError> read_ui_to_engine(const nlohmann::json& value) {
    if (!value.is_object()) {
        return common::make_unexpected(DecodeError{DecodeErrorCode::kNotObject, {}});
    }
    ObjectReader r{value};
    const auto type = r.text("type", kTypeRule);
    if (!r.ok()) {
        // type がないか不正なら、ほかのフィールドは読まずに返す。
        return common::make_unexpected(r.finish().error());
    }

    std::optional<UiToEngineMessage> message;
    const auto read_tagged = [&r](auto tag) { return read_body<typename decltype(tag)::type>(r); };
    if (auto body = read_alternative<DiscreteCommandBody>(type, read_tagged)) {
        DiscreteCommand command;
        command.request_id = r.text("request_id", kIdentifierRule);
        command.entity_id = r.text("entity_id", kIdentifierRule);
        command.expected_revision = r.counter("expected_revision");
        command.body = std::move(*body);
        message = std::move(command);
    } else if (auto other = read_alternative<OtherMessage>(type, read_tagged)) {
        message = std::visit([](auto&& alternative) -> UiToEngineMessage { return alternative; },
                             std::move(*other));
    }
    if (!message) {
        return common::make_unexpected(DecodeError{DecodeErrorCode::kUnknownType, "type"});
    }
    if (auto finished = r.finish(); !finished) {
        return common::make_unexpected(finished.error());
    }
    return std::move(*message);
}

}  // namespace vocroz::control_protocol::wire
