// SPDX-License-Identifier: AGPL-3.0-only
//
// 状態の DTO と JSON の変換の実装。フィールドの名前と規則は、書き出しと読み込みで同じ順に並べる。

#include "wire/entity_codec.h"

#include "vocroz/control_protocol/constants.h"

#include "wire/field_rules.h"
#include "wire/json_values.h"
#include "wire/object_reader.h"

namespace vocroz::control_protocol::wire {
namespace {

// 無名名前空間の write_json() が公開の write_json() を隠さないようにする。
using wire::write_json;

// 1 以上 max 以下の小さな整数（チャンネル数、入力の番号）を読む。
std::uint8_t small_count(ObjectReader& r, std::string_view key, std::size_t max) {
    const std::uint64_t value = r.counter(key);
    if (value < 1 || value > max) {
        r.fail(DecodeErrorCode::kOutOfRange, key);
        return 1;
    }
    return static_cast<std::uint8_t>(value);
}

// ルーティングの構成の要素は構成の中でだけ使うので、公開せずにここで変換する。

nlohmann::json write_json(const RoutingBus& bus) {
    return {
        {"entity_id", bus.entity_id},
        {"name", bus.name},
        {"kind", enum_json(bus.kind)},
    };
}

common::Result<RoutingBus, DecodeError> read_routing_bus(const nlohmann::json& value) {
    return read_object<RoutingBus>(value, [](ObjectReader& r) {
        RoutingBus bus;
        bus.entity_id = r.text("entity_id", kIdentifierRule);
        bus.name = r.text("name", kTextRule);
        bus.kind = r.enumeration<BusKind>("kind");
        return bus;
    });
}

nlohmann::json write_json(const RoutingOutput& output) {
    return {
        {"entity_id", output.entity_id},
        {"name", output.name},
        {"device_channel", output.device_channel},
    };
}

common::Result<RoutingOutput, DecodeError> read_routing_output(const nlohmann::json& value) {
    return read_object<RoutingOutput>(value, [](ObjectReader& r) {
        RoutingOutput output;
        output.entity_id = r.text("entity_id", kIdentifierRule);
        output.name = r.text("name", kTextRule);
        // チャンネル数の上限は出力デバイスとミキサーの知識なので、範囲は session が確かめる。
        output.device_channel = r.counter("device_channel");
        return output;
    });
}

nlohmann::json write_json(const RoutingSend& send) {
    return {
        {"entity_id", send.entity_id},
        {"from", send.from},
        {"to", send.to},
        {"tap", enum_json(send.tap)},
    };
}

common::Result<RoutingSend, DecodeError> read_routing_send(const nlohmann::json& value) {
    return read_object<RoutingSend>(value, [](ObjectReader& r) {
        RoutingSend send;
        send.entity_id = r.text("entity_id", kIdentifierRule);
        send.from = r.text("from", kIdentifierRule);
        send.to = r.text("to", kIdentifierRule);
        send.tap = r.enumeration<SendTap>("tap");
        return send;
    });
}

nlohmann::json write_json(const RoutingDelay& delay) {
    return {
        {"entity_id", delay.entity_id},
        {"delay_ms", delay.delay_ms},
    };
}

common::Result<RoutingDelay, DecodeError> read_routing_delay(const nlohmann::json& value) {
    return read_object<RoutingDelay>(value, [](ObjectReader& r) {
        RoutingDelay delay;
        delay.entity_id = r.text("entity_id", kIdentifierRule);
        delay.delay_ms = r.number("delay_ms", 0.0, kMaxRoutingDelayMs);
        return delay;
    });
}

nlohmann::json write_json(const InputDelay& delay) {
    return {
        {"strip_id", delay.strip_id},
        {"delay_ms", delay.delay_ms},
    };
}

common::Result<InputDelay, DecodeError> read_input_delay(const nlohmann::json& value) {
    return read_object<InputDelay>(value, [](ObjectReader& r) {
        InputDelay delay;
        delay.strip_id = r.text("strip_id", kIdentifierRule);
        delay.delay_ms = r.number("delay_ms", 0.0, kMaxRoutingDelayMs);
        return delay;
    });
}

/// entity_id の文字列を 1 つ読む（配列の要素）。
common::Result<std::string, DecodeError> read_identifier(const nlohmann::json& value) {
    if (!value.is_string()) {
        return common::make_unexpected(DecodeError{DecodeErrorCode::kWrongType, {}});
    }
    const auto& text = value.get_ref<const std::string&>();
    if (!satisfies(text, kIdentifierRule)) {
        return common::make_unexpected(DecodeError{DecodeErrorCode::kOutOfRange, {}});
    }
    return text;
}

nlohmann::json write_json(const RoutingCheck& check) {
    return {
        {"issue", enum_json(check.issue)},
        {"issue_entity_id", nullable(check.issue_entity_id)},
        {"looped_entity_ids", check.looped_entity_ids},
        {"needs_approval", check.needs_approval},
    };
}

common::Result<RoutingCheck, DecodeError> read_routing_check(const nlohmann::json& value) {
    return read_object<RoutingCheck>(value, [](ObjectReader& r) {
        RoutingCheck check;
        check.issue = r.nullable_enumeration<RoutingIssue>("issue");
        check.issue_entity_id = r.nullable_text("issue_entity_id", kIdentifierRule);
        check.looped_entity_ids = r.list<std::string>("looped_entity_ids", read_identifier);
        check.needs_approval = r.boolean("needs_approval");
        return check;
    });
}

nlohmann::json write_json(const StagedRouting& staged) {
    return {
        {"config", write_json(staged.config)},
        {"check", write_json(staged.check)},
    };
}

common::Result<StagedRouting, DecodeError> read_staged_routing(const nlohmann::json& value) {
    return read_object<StagedRouting>(value, [](ObjectReader& r) {
        StagedRouting staged;
        staged.config = r.nested<RoutingConfig>("config", read_routing_config);
        staged.check = r.nested<RoutingCheck>("check", read_routing_check);
        return staged;
    });
}

// 要素の write_json() より後に置き、配列の要素の型の write_json() を見つけられるようにする。
/// DTO の配列を JSON の配列にする。
template <typename T>
nlohmann::json write_array(const std::vector<T>& items) {
    auto array = nlohmann::json::array();
    for (const auto& item : items) {
        array.push_back(write_json(item));
    }
    return array;
}

}  // namespace

nlohmann::json write_json(const OutputDevice& device) {
    return {
        {"id", device.id},
        {"name", device.name},
    };
}

common::Result<OutputDevice, DecodeError> read_output_device(const nlohmann::json& value) {
    return read_object<OutputDevice>(value, [](ObjectReader& r) {
        OutputDevice device;
        device.id = r.text("id", kDeviceIdRule);
        device.name = r.text("name", kTextRule);
        return device;
    });
}

nlohmann::json write_json(const SessionInfo& session) {
    return {
        {"entity_id", session.entity_id},
        {"revision", session.revision},
        {"name", session.name},
        {"state", enum_json(session.state)},
        {"error_code", enum_json(session.error_code)},
        {"invite_url", nullable(session.invite_url)},
        {"output_device_state", enum_json(session.output_device_state)},
        {"output_device_name", nullable(session.output_device_name)},
        {"output_device_id", nullable(session.output_device_id)},
        {"output_devices", write_array(session.output_devices)},
        {"update_available", nullable(session.update_available)},
        {"latency_target_ms", session.latency_target_ms},
    };
}

common::Result<SessionInfo, DecodeError> read_session_info(const nlohmann::json& value) {
    return read_object<SessionInfo>(value, [](ObjectReader& r) {
        SessionInfo session;
        session.entity_id = r.text("entity_id", kIdentifierRule);
        session.revision = r.counter("revision");
        session.name = r.text("name", kTextRule);
        session.state = r.enumeration<SessionState>("state");
        session.error_code = r.nullable_enumeration<ErrorCode>("error_code");
        session.invite_url = r.nullable_text("invite_url", kUrlRule);
        session.output_device_state = r.enumeration<OutputDeviceState>("output_device_state");
        session.output_device_name = r.nullable_text("output_device_name", kTextRule);
        session.output_device_id = r.nullable_text("output_device_id", kDeviceIdRule);
        session.output_devices = r.list<OutputDevice>("output_devices", read_output_device);
        session.update_available = r.nullable_text("update_available", kTextRule);
        session.latency_target_ms =
            r.number("latency_target_ms", kMinLatencyTargetMs, kMaxLatencyTargetMs);
        return session;
    });
}

nlohmann::json write_json(const ChannelStrip& channel) {
    return {
        {"entity_id", channel.entity_id},
        {"revision", channel.revision},
        {"participant_id", channel.participant_id},
        {"name", channel.name},
        {"short_id", channel.short_id},
        {"connection_state", enum_json(channel.connection_state)},
        {"signal_state", enum_json(channel.signal_state)},
        {"send_muted", channel.send_muted},
        {"channel_muted", channel.channel_muted},
        {"gain_db", nullable(channel.gain_db)},
        {"error_code", enum_json(channel.error_code)},
        {"source_kind", enum_json(channel.source_kind)},
        {"channel_count", channel.channel_count},
        {"source_number", channel.source_number},
        {"return_muted", channel.return_muted},
    };
}

common::Result<ChannelStrip, DecodeError> read_channel_strip(const nlohmann::json& value) {
    return read_object<ChannelStrip>(value, [](ObjectReader& r) {
        ChannelStrip channel;
        channel.entity_id = r.text("entity_id", kIdentifierRule);
        channel.revision = r.counter("revision");
        channel.participant_id = r.text("participant_id", kIdentifierRule);
        channel.name = r.text("name", kTextRule);
        channel.short_id = r.text("short_id", kTextRule);
        channel.connection_state = r.enumeration<ConnectionState>("connection_state");
        // PENDING の参加者はストリップを持たないので、ストリップの状態としては不正。
        if (channel.connection_state == ConnectionState::kPending) {
            r.fail(DecodeErrorCode::kOutOfRange, "connection_state");
        }
        channel.signal_state = r.nullable_enumeration<SignalState>("signal_state");
        channel.send_muted = r.boolean("send_muted");
        channel.channel_muted = r.boolean("channel_muted");
        channel.gain_db = r.gain("gain_db");
        channel.error_code = r.nullable_enumeration<ErrorCode>("error_code");
        channel.source_kind = r.enumeration<InputSourceKind>("source_kind");
        channel.channel_count = small_count(r, "channel_count", kMaxInputChannels);
        channel.source_number = small_count(r, "source_number", kMaxInputStreams);
        channel.return_muted = r.boolean("return_muted");
        return channel;
    });
}

nlohmann::json write_json(const MasterStrip& master) {
    return {
        {"entity_id", master.entity_id},
        {"revision", master.revision},
        {"gain_db", nullable(master.gain_db)},
        {"master_muted", master.master_muted},
        {"master_mute_reason", enum_json(master.master_mute_reason)},
    };
}

common::Result<MasterStrip, DecodeError> read_master_strip(const nlohmann::json& value) {
    return read_object<MasterStrip>(value, [](ObjectReader& r) {
        MasterStrip master;
        master.entity_id = r.text("entity_id", kIdentifierRule);
        master.revision = r.counter("revision");
        master.gain_db = r.gain("gain_db");
        master.master_muted = r.boolean("master_muted");
        master.master_mute_reason = r.nullable_enumeration<MasterMuteReason>("master_mute_reason");
        return master;
    });
}

nlohmann::json write_json(const PendingParticipant& participant) {
    return {
        {"entity_id", participant.entity_id},
        {"revision", participant.revision},
        {"name", participant.name},
        {"short_id", participant.short_id},
    };
}

common::Result<PendingParticipant, DecodeError> read_pending_participant(
    const nlohmann::json& value) {
    return read_object<PendingParticipant>(value, [](ObjectReader& r) {
        PendingParticipant participant;
        participant.entity_id = r.text("entity_id", kIdentifierRule);
        participant.revision = r.counter("revision");
        participant.name = r.text("name", kTextRule);
        participant.short_id = r.text("short_id", kTextRule);
        return participant;
    });
}

nlohmann::json write_json(const RoutingConfig& config) {
    return {
        {"buses", write_array(config.buses)},
        {"outputs", write_array(config.outputs)},
        {"sends", write_array(config.sends)},
        {"delays", write_array(config.delays)},
        {"input_delays", write_array(config.input_delays)},
    };
}

common::Result<RoutingConfig, DecodeError> read_routing_config(const nlohmann::json& value) {
    return read_object<RoutingConfig>(value, [](ObjectReader& r) {
        RoutingConfig config;
        config.buses = r.list<RoutingBus>("buses", read_routing_bus);
        config.outputs = r.list<RoutingOutput>("outputs", read_routing_output);
        config.sends = r.list<RoutingSend>("sends", read_routing_send);
        config.delays = r.list<RoutingDelay>("delays", read_routing_delay);
        config.input_delays = r.list<InputDelay>("input_delays", read_input_delay);
        return config;
    });
}

nlohmann::json write_json(const RouteLevel& level) {
    return {
        {"entity_id", level.entity_id},
        {"revision", level.revision},
        {"gain_db", nullable(level.gain_db)},
        {"muted", level.muted},
    };
}

common::Result<RouteLevel, DecodeError> read_route_level(const nlohmann::json& value) {
    return read_object<RouteLevel>(value, [](ObjectReader& r) {
        RouteLevel level;
        level.entity_id = r.text("entity_id", kIdentifierRule);
        level.revision = r.counter("revision");
        level.gain_db = r.gain("gain_db");
        level.muted = r.boolean("muted");
        return level;
    });
}

nlohmann::json write_json(const RoutingInfo& routing) {
    return {
        {"entity_id", routing.entity_id},
        {"revision", routing.revision},
        {"applied", write_json(routing.applied)},
        {"feedback_approved", routing.feedback_approved},
        {"staged", routing.staged ? write_json(*routing.staged) : nlohmann::json(nullptr)},
        {"levels", write_array(routing.levels)},
    };
}

common::Result<RoutingInfo, DecodeError> read_routing_info(const nlohmann::json& value) {
    return read_object<RoutingInfo>(value, [](ObjectReader& r) {
        RoutingInfo routing;
        routing.entity_id = r.text("entity_id", kIdentifierRule);
        routing.revision = r.counter("revision");
        routing.applied = r.nested<RoutingConfig>("applied", read_routing_config);
        routing.feedback_approved = r.boolean("feedback_approved");
        routing.staged = r.nullable_nested<StagedRouting>("staged", read_staged_routing);
        routing.levels = r.list<RouteLevel>("levels", read_route_level);
        return routing;
    });
}

nlohmann::json write_json(const MixerSnapshot& snapshot) {
    return {
        {"session", write_json(snapshot.session)}, {"channels", write_array(snapshot.channels)},
        {"master", write_json(snapshot.master)},   {"pending", write_array(snapshot.pending)},
        {"routing", write_json(snapshot.routing)},
    };
}

common::Result<MixerSnapshot, DecodeError> read_mixer_snapshot(const nlohmann::json& value) {
    return read_object<MixerSnapshot>(value, [](ObjectReader& r) {
        MixerSnapshot snapshot;
        snapshot.session = r.nested<SessionInfo>("session", read_session_info);
        snapshot.channels = r.list<ChannelStrip>("channels", read_channel_strip);
        snapshot.master = r.nested<MasterStrip>("master", read_master_strip);
        snapshot.pending = r.list<PendingParticipant>("pending", read_pending_participant);
        snapshot.routing = r.nested<RoutingInfo>("routing", read_routing_info);
        return snapshot;
    });
}

nlohmann::json write_json(const InputDevice& device) {
    return {
        {"id", device.id},
        {"name", device.name},
    };
}

common::Result<InputDevice, DecodeError> read_input_device(const nlohmann::json& value) {
    return read_object<InputDevice>(value, [](ObjectReader& r) {
        InputDevice device;
        device.id = r.text("id", kDeviceIdRule);
        device.name = r.text("name", kTextRule);
        return device;
    });
}

nlohmann::json write_json(const ClientSnapshot& snapshot) {
    return {
        {"entity_id", snapshot.entity_id},
        {"revision", snapshot.revision},
        {"client_state", enum_json(snapshot.client_state)},
        {"failure_code", enum_json(snapshot.failure_code)},
        {"session_name", nullable(snapshot.session_name)},
        {"input_devices", write_array(snapshot.input_devices)},
        {"selected_input_id", nullable(snapshot.selected_input_id)},
        {"input_missing", snapshot.input_missing},
        {"send_muted", snapshot.send_muted},
        {"send_level_db", snapshot.send_level_db},
        {"return_muted", snapshot.return_muted},
    };
}

common::Result<ClientSnapshot, DecodeError> read_client_snapshot(const nlohmann::json& value) {
    return read_object<ClientSnapshot>(value, [](ObjectReader& r) {
        ClientSnapshot snapshot;
        snapshot.entity_id = r.text("entity_id", kIdentifierRule);
        snapshot.revision = r.counter("revision");
        snapshot.client_state = r.enumeration<ClientState>("client_state");
        snapshot.failure_code = r.nullable_enumeration<ErrorCode>("failure_code");
        snapshot.session_name = r.nullable_text("session_name", kTextRule);
        snapshot.input_devices = r.list<InputDevice>("input_devices", read_input_device);
        snapshot.selected_input_id = r.nullable_text("selected_input_id", kDeviceIdRule);
        snapshot.input_missing = r.boolean("input_missing");
        snapshot.send_muted = r.boolean("send_muted");
        snapshot.send_level_db = r.number("send_level_db", kMinFiniteGainDb, kMaxGainDb);
        snapshot.return_muted = r.boolean("return_muted");
        return snapshot;
    });
}

}  // namespace vocroz::control_protocol::wire
