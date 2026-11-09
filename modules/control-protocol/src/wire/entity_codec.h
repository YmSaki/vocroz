// SPDX-License-Identifier: AGPL-3.0-only
//
// 状態の DTO（entities.h）と JSON の変換。スナップショットと差分イベントで共通に使う。
#pragma once

#include <nlohmann/json.hpp>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/entities.h"
#include "vocroz/control_protocol/json_codec.h"

namespace vocroz::control_protocol::wire {

[[nodiscard]] nlohmann::json write_json(const OutputDevice& device);
[[nodiscard]] nlohmann::json write_json(const SessionInfo& session);
[[nodiscard]] nlohmann::json write_json(const ChannelStrip& channel);
[[nodiscard]] nlohmann::json write_json(const MasterStrip& master);
[[nodiscard]] nlohmann::json write_json(const PendingParticipant& participant);
[[nodiscard]] nlohmann::json write_json(const RoutingConfig& config);
[[nodiscard]] nlohmann::json write_json(const RouteLevel& level);
[[nodiscard]] nlohmann::json write_json(const RoutingInfo& routing);
[[nodiscard]] nlohmann::json write_json(const ExternalOutputConfig& config);
[[nodiscard]] nlohmann::json write_json(const ExternalOutput& output);
[[nodiscard]] nlohmann::json write_json(const MixerSnapshot& snapshot);
[[nodiscard]] nlohmann::json write_json(const InputDevice& device);
[[nodiscard]] nlohmann::json write_json(const ClientSnapshot& snapshot);

[[nodiscard]] common::Result<OutputDevice, DecodeError> read_output_device(
    const nlohmann::json& value);
[[nodiscard]] common::Result<SessionInfo, DecodeError> read_session_info(
    const nlohmann::json& value);
[[nodiscard]] common::Result<ChannelStrip, DecodeError> read_channel_strip(
    const nlohmann::json& value);
[[nodiscard]] common::Result<MasterStrip, DecodeError> read_master_strip(
    const nlohmann::json& value);
[[nodiscard]] common::Result<PendingParticipant, DecodeError> read_pending_participant(
    const nlohmann::json& value);
[[nodiscard]] common::Result<RoutingConfig, DecodeError> read_routing_config(
    const nlohmann::json& value);
[[nodiscard]] common::Result<RouteLevel, DecodeError> read_route_level(const nlohmann::json& value);
[[nodiscard]] common::Result<RoutingInfo, DecodeError> read_routing_info(
    const nlohmann::json& value);
[[nodiscard]] common::Result<ExternalOutputConfig, DecodeError> read_external_output_config(
    const nlohmann::json& value);
[[nodiscard]] common::Result<ExternalOutput, DecodeError> read_external_output(
    const nlohmann::json& value);
[[nodiscard]] common::Result<MixerSnapshot, DecodeError> read_mixer_snapshot(
    const nlohmann::json& value);
[[nodiscard]] common::Result<InputDevice, DecodeError> read_input_device(
    const nlohmann::json& value);
[[nodiscard]] common::Result<ClientSnapshot, DecodeError> read_client_snapshot(
    const nlohmann::json& value);

}  // namespace vocroz::control_protocol::wire
