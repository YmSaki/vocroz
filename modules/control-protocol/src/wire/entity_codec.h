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
[[nodiscard]] nlohmann::json write_json(const PluginEntry& entry);
[[nodiscard]] nlohmann::json write_json(const BlockedPluginFile& file);
[[nodiscard]] nlohmann::json write_json(const PluginInsertStatus& status);
[[nodiscard]] nlohmann::json write_json(const PluginsInfo& plugins);
[[nodiscard]] nlohmann::json write_json(const RecorderConfig& config);
[[nodiscard]] nlohmann::json write_json(const RecordingTrackInfo& track);
[[nodiscard]] nlohmann::json write_json(const Recorder& recorder);
[[nodiscard]] nlohmann::json write_json(const MediaFile& file);
[[nodiscard]] nlohmann::json write_json(const MediaPlayer& player);
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
[[nodiscard]] common::Result<PluginEntry, DecodeError> read_plugin_entry(
    const nlohmann::json& value);
[[nodiscard]] common::Result<PluginsInfo, DecodeError> read_plugins_info(
    const nlohmann::json& value);
[[nodiscard]] common::Result<RecorderConfig, DecodeError> read_recorder_config(
    const nlohmann::json& value);
[[nodiscard]] common::Result<RecordingTrackInfo, DecodeError> read_recording_track_info(
    const nlohmann::json& value);
[[nodiscard]] common::Result<Recorder, DecodeError> read_recorder(const nlohmann::json& value);
[[nodiscard]] common::Result<MediaFile, DecodeError> read_media_file(const nlohmann::json& value);
[[nodiscard]] common::Result<MediaPlayer, DecodeError> read_media_player(
    const nlohmann::json& value);
[[nodiscard]] common::Result<MixerSnapshot, DecodeError> read_mixer_snapshot(
    const nlohmann::json& value);
[[nodiscard]] common::Result<InputDevice, DecodeError> read_input_device(
    const nlohmann::json& value);
[[nodiscard]] common::Result<ClientSnapshot, DecodeError> read_client_snapshot(
    const nlohmann::json& value);

}  // namespace vocroz::control_protocol::wire
