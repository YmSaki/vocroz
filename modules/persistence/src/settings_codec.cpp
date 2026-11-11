// SPDX-License-Identifier: AGPL-3.0-only
//
// 設定ファイルの JSON の変換の実装。
// nlohmann/json は例外を送出しない使い方（parse の第 3 引数を false）にして、
// 壊れたファイルを例外ではなく std::nullopt として扱う。

#include "settings_codec.h"

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "vocroz/control_protocol/constants.h"

#include "persistence_constants.h"

namespace vocroz::persistence {
namespace {

using nlohmann::json;

constexpr const char* kSchemaVersionKey = "schema_version";

// 形式のバージョンが合うオブジェクトだけを受け付ける。
std::optional<json> parse_object(std::string_view text) {
    auto root = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (!root.is_object()) {
        return std::nullopt;
    }
    const auto version = root.find(kSchemaVersionKey);
    if (version == root.end() || !version->is_number_integer() ||
        version->get<std::int64_t>() != kSettingsSchemaVersion) {
        return std::nullopt;
    }
    return root;
}

std::optional<std::string> read_string(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

// 値が null なら「未選択」として std::optional の空を返す。
std::optional<std::optional<std::string>> read_optional_string(const json& object,
                                                               const char* key) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return std::nullopt;
    }
    if (it->is_null()) {
        return std::optional<std::string>{};
    }
    if (!it->is_string()) {
        return std::nullopt;
    }
    return std::optional<std::string>{it->get<std::string>()};
}

std::optional<double> read_number(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number()) {
        return std::nullopt;
    }
    return it->get<double>();
}

// フェーダーの値。null は -∞ dB（control-protocol の GainDb と同じ表し方）。
std::optional<control_protocol::GainDb> read_gain(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return std::nullopt;
    }
    if (it->is_null()) {
        return control_protocol::GainDb{};
    }
    if (!it->is_number()) {
        return std::nullopt;
    }
    return control_protocol::GainDb{it->get<double>()};
}

json optional_to_json(const std::optional<std::string>& value) {
    return value ? json(*value) : json(nullptr);
}

json gain_to_json(const control_protocol::GainDb& gain) {
    return gain ? json(*gain) : json(nullptr);
}

// --- ルーティング ---
// 列挙型の名前は設定ファイルの形式として固定する。control-protocol の名前とは変更の理由が
// 違う（UI との形式を変えても保存済みのファイルは読めなければならない）ので、ここで別に持つ。

const char* bus_kind_name(control_protocol::BusKind kind) {
    switch (kind) {
        case control_protocol::BusKind::kUser:
            return "user";
        case control_protocol::BusKind::kCue:
            return "cue";
    }
    return "user";
}

std::optional<control_protocol::BusKind> read_bus_kind(const json& object) {
    const auto name = read_string(object, "kind");
    if (name == "user") {
        return control_protocol::BusKind::kUser;
    }
    if (name == "cue") {
        return control_protocol::BusKind::kCue;
    }
    return std::nullopt;
}

const char* send_tap_name(control_protocol::SendTap tap) {
    switch (tap) {
        case control_protocol::SendTap::kPostFader:
            return "post_fader";
        case control_protocol::SendTap::kPreFader:
            return "pre_fader";
    }
    return "post_fader";
}

std::optional<control_protocol::SendTap> read_send_tap(const json& object) {
    const auto name = read_string(object, "tap");
    if (name == "post_fader") {
        return control_protocol::SendTap::kPostFader;
    }
    if (name == "pre_fader") {
        return control_protocol::SendTap::kPreFader;
    }
    return std::nullopt;
}

std::optional<std::uint64_t> read_unsigned(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_unsigned()) {
        return std::nullopt;
    }
    return it->get<std::uint64_t>();
}

json routing_to_json(const control_protocol::RoutingConfig& config) {
    json buses = json::array();
    for (const auto& bus : config.buses) {
        buses.push_back(
            {{"entity_id", bus.entity_id}, {"name", bus.name}, {"kind", bus_kind_name(bus.kind)}});
    }
    json outputs = json::array();
    for (const auto& output : config.outputs) {
        outputs.push_back({{"entity_id", output.entity_id},
                           {"name", output.name},
                           {"device_channel", output.device_channel}});
    }
    json sends = json::array();
    for (const auto& send : config.sends) {
        sends.push_back({{"entity_id", send.entity_id},
                         {"from", send.from},
                         {"to", send.to},
                         {"tap", send_tap_name(send.tap)}});
    }
    json delays = json::array();
    for (const auto& delay : config.delays) {
        delays.push_back({{"entity_id", delay.entity_id}, {"delay_ms", delay.delay_ms}});
    }
    json input_delays = json::array();
    for (const auto& delay : config.input_delays) {
        input_delays.push_back({{"strip_id", delay.strip_id}, {"delay_ms", delay.delay_ms}});
    }
    json inserts = json::array();
    for (const auto& insert : config.inserts) {
        inserts.push_back({{"entity_id", insert.entity_id},
                           {"target", insert.target},
                           {"plugin_id", insert.plugin_id}});
    }
    return {{"buses", buses},   {"outputs", outputs},           {"sends", sends},
            {"delays", delays}, {"input_delays", input_delays}, {"inserts", inserts}};
}

// 配列の要素を 1 つずつ読む。要素が 1 つでも読めなければ、配列全体を読めなかったものとする。
template <typename T, typename ReadElement>
bool read_array(const json& object, const char* key, std::vector<T>& out, ReadElement read) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array()) {
        return false;
    }
    for (const auto& element : *it) {
        if (!element.is_object()) {
            return false;
        }
        auto value = read(element);
        if (!value) {
            return false;
        }
        out.push_back(std::move(*value));
    }
    return true;
}

std::optional<control_protocol::RoutingConfig> read_routing(const json& object) {
    namespace cp = control_protocol;
    cp::RoutingConfig config;
    const bool ok =
        read_array(object, "buses", config.buses,
                   [](const json& e) -> std::optional<cp::RoutingBus> {
                       auto id = read_string(e, "entity_id");
                       auto name = read_string(e, "name");
                       const auto kind = read_bus_kind(e);
                       if (!id || !name || !kind) {
                           return std::nullopt;
                       }
                       return cp::RoutingBus{std::move(*id), std::move(*name), *kind};
                   }) &&
        read_array(object, "outputs", config.outputs,
                   [](const json& e) -> std::optional<cp::RoutingOutput> {
                       auto id = read_string(e, "entity_id");
                       auto name = read_string(e, "name");
                       const auto channel = read_unsigned(e, "device_channel");
                       if (!id || !name || !channel) {
                           return std::nullopt;
                       }
                       return cp::RoutingOutput{std::move(*id), std::move(*name), *channel};
                   }) &&
        read_array(object, "sends", config.sends,
                   [](const json& e) -> std::optional<cp::RoutingSend> {
                       auto id = read_string(e, "entity_id");
                       auto from = read_string(e, "from");
                       auto to = read_string(e, "to");
                       const auto tap = read_send_tap(e);
                       if (!id || !from || !to || !tap) {
                           return std::nullopt;
                       }
                       return cp::RoutingSend{std::move(*id), std::move(*from), std::move(*to),
                                              *tap};
                   }) &&
        read_array(object, "delays", config.delays,
                   [](const json& e) -> std::optional<cp::RoutingDelay> {
                       auto id = read_string(e, "entity_id");
                       const auto delay_ms = read_number(e, "delay_ms");
                       if (!id || !delay_ms) {
                           return std::nullopt;
                       }
                       return cp::RoutingDelay{std::move(*id), *delay_ms};
                   }) &&
        read_array(object, "input_delays", config.input_delays,
                   [](const json& e) -> std::optional<cp::InputDelay> {
                       auto id = read_string(e, "strip_id");
                       const auto delay_ms = read_number(e, "delay_ms");
                       if (!id || !delay_ms) {
                           return std::nullopt;
                       }
                       return cp::InputDelay{std::move(*id), *delay_ms};
                   }) &&
        // 挿入を保存する前のバージョンのファイルには inserts がない。挿入なしとして読む。
        (!object.contains("inserts") ||
         read_array(object, "inserts", config.inserts,
                    [](const json& e) -> std::optional<cp::RoutingInsert> {
                        auto id = read_string(e, "entity_id");
                        auto target = read_string(e, "target");
                        auto plugin_id = read_string(e, "plugin_id");
                        if (!id || !target || !plugin_id) {
                            return std::nullopt;
                        }
                        return cp::RoutingInsert{std::move(*id), std::move(*target),
                                                 std::move(*plugin_id)};
                    }));
    if (!ok) {
        return std::nullopt;
    }
    return config;
}

std::optional<std::vector<session::SavedRouteGain>> read_route_gains(const json& root) {
    std::vector<session::SavedRouteGain> gains;
    if (!read_array(root, "route_gains", gains,
                    [](const json& e) -> std::optional<session::SavedRouteGain> {
                        auto id = read_string(e, "entity_id");
                        const auto gain = read_gain(e, "gain_db");
                        if (!id || !gain) {
                            return std::nullopt;
                        }
                        return session::SavedRouteGain{std::move(*id), *gain};
                    })) {
        return std::nullopt;
    }
    return gains;
}

const char* profile_name(control_protocol::ExternalOutputProfile profile) {
    switch (profile) {
        case control_protocol::ExternalOutputProfile::kOpusStereo:
            return "opus_stereo";
        case control_protocol::ExternalOutputProfile::kOpusMono:
            return "opus_mono";
    }
    return "opus_stereo";
}

std::optional<control_protocol::ExternalOutputProfile> read_profile(const json& object) {
    const auto name = read_string(object, "profile");
    if (name == "opus_stereo") {
        return control_protocol::ExternalOutputProfile::kOpusStereo;
    }
    if (name == "opus_mono") {
        return control_protocol::ExternalOutputProfile::kOpusMono;
    }
    return std::nullopt;
}

const char* suite_name(control_protocol::SrtpSuite suite) {
    switch (suite) {
        case control_protocol::SrtpSuite::kAesCm128HmacSha1_80:
            return "aes_cm_128_hmac_sha1_80";
        case control_protocol::SrtpSuite::kAesCm128HmacSha1_32:
            return "aes_cm_128_hmac_sha1_32";
    }
    return "aes_cm_128_hmac_sha1_80";
}

std::optional<control_protocol::SrtpSuite> read_suite(const json& object) {
    const auto name = read_string(object, "crypto_suite");
    if (name == "aes_cm_128_hmac_sha1_80") {
        return control_protocol::SrtpSuite::kAesCm128HmacSha1_80;
    }
    if (name == "aes_cm_128_hmac_sha1_32") {
        return control_protocol::SrtpSuite::kAesCm128HmacSha1_32;
    }
    return std::nullopt;
}

// SRTP 外部出力の設定。鍵は秘密情報なので、ここではなく SecretStore に保存する。
json external_output_to_json(const control_protocol::ExternalOutputConfig& config) {
    return {{"host", config.host},
            {"port", config.port},
            {"source_id", config.source_id},
            {"profile", profile_name(config.profile)},
            {"payload_type", config.payload_type},
            {"crypto_suite", suite_name(config.crypto_suite)}};
}

std::optional<control_protocol::ExternalOutputConfig> read_external_output(const json& object) {
    auto host = read_string(object, "host");
    const auto port = read_unsigned(object, "port");
    auto source_id = read_string(object, "source_id");
    const auto profile = read_profile(object);
    const auto payload_type = read_unsigned(object, "payload_type");
    const auto suite = read_suite(object);
    if (!host || !port || !source_id || !profile || !payload_type || !suite || *port == 0 ||
        *port > std::numeric_limits<std::uint16_t>::max() ||
        *payload_type < control_protocol::kMinExternalOutputPayloadType ||
        *payload_type > control_protocol::kMaxExternalOutputPayloadType) {
        return std::nullopt;
    }
    control_protocol::ExternalOutputConfig config;
    config.host = std::move(*host);
    config.port = static_cast<std::uint16_t>(*port);
    config.source_id = std::move(*source_id);
    config.profile = *profile;
    config.payload_type = static_cast<std::uint8_t>(*payload_type);
    config.crypto_suite = *suite;
    return config;
}

// 表示名などに不正な UTF-8 が入っていても保存は止めず、置換文字にして書く。
std::string dump(const json& root) {
    return root.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

}  // namespace

std::string encode_host_settings(const session::HostSettings& settings) {
    json root = json::object();
    root[kSchemaVersionKey] = kSettingsSchemaVersion;
    root["session_name"] = settings.session_name;
    root["output_device_id"] = optional_to_json(settings.output_device_id);
    root["master_gain_db"] = gain_to_json(settings.master_gain_db);
    root["routing"] = routing_to_json(settings.routing);
    json gains = json::array();
    for (const auto& gain : settings.route_gains) {
        gains.push_back({{"entity_id", gain.entity_id}, {"gain_db", gain_to_json(gain.gain_db)}});
    }
    root["route_gains"] = std::move(gains);
    root["latency_target_ms"] = settings.latency_target_ms;
    root["external_output"] = external_output_to_json(settings.external_output);
    return dump(root);
}

std::optional<session::HostSettings> decode_host_settings(std::string_view text) {
    const auto root = parse_object(text);
    if (!root) {
        return std::nullopt;
    }
    auto session_name = read_string(*root, "session_name");
    auto output_device_id = read_optional_string(*root, "output_device_id");
    const auto master_gain_db = read_gain(*root, "master_gain_db");
    if (!session_name || !output_device_id || !master_gain_db) {
        return std::nullopt;
    }
    session::HostSettings settings;
    settings.session_name = std::move(*session_name);
    settings.output_device_id = std::move(*output_device_id);
    settings.master_gain_db = *master_gain_db;
    // ルーティングを保存する前のバージョンのファイルには routing と route_gains がない。
    // その場合は既定の結線（構成なし）として読み、ほかの設定は引き継ぐ。
    if (root->contains("routing") || root->contains("route_gains")) {
        const auto routing = root->find("routing");
        if (routing == root->end() || !routing->is_object()) {
            return std::nullopt;
        }
        auto config = read_routing(*routing);
        auto gains = read_route_gains(*root);
        if (!config || !gains) {
            return std::nullopt;
        }
        settings.routing = std::move(*config);
        settings.route_gains = std::move(*gains);
    }
    // 目標レイテンシーを保存する前のバージョンのファイルにはこの値がないので、既定値のまま読む。
    // 範囲の外の値はここでは拒まず、起動時にセッションが既定値に戻す。
    if (root->contains("latency_target_ms")) {
        const auto latency = read_number(*root, "latency_target_ms");
        if (!latency) {
            return std::nullopt;
        }
        settings.latency_target_ms = *latency;
    }
    // SRTP 外部出力を保存する前のバージョンのファイルにはこの値がないので、未設定のまま読む。
    if (root->contains("external_output")) {
        const auto section = root->find("external_output");
        if (!section->is_object()) {
            return std::nullopt;
        }
        auto external_output = read_external_output(*section);
        if (!external_output) {
            return std::nullopt;
        }
        settings.external_output = std::move(*external_output);
    }
    return settings;
}

std::string encode_client_settings(const session::ClientSettings& settings) {
    json root = json::object();
    root[kSchemaVersionKey] = kSettingsSchemaVersion;
    root["display_name"] = settings.display_name;
    root["selected_input_id"] = optional_to_json(settings.selected_input_id);
    root["send_level_db"] = settings.send_level_db;
    root["input_channels"] = settings.input_channels;
    root["output_device_id"] = optional_to_json(settings.output_device_id);
    return dump(root);
}

std::optional<session::ClientSettings> decode_client_settings(std::string_view text) {
    const auto root = parse_object(text);
    if (!root) {
        return std::nullopt;
    }
    auto display_name = read_string(*root, "display_name");
    auto selected_input_id = read_optional_string(*root, "selected_input_id");
    const auto send_level_db = read_number(*root, "send_level_db");
    if (!display_name || !selected_input_id || !send_level_db) {
        return std::nullopt;
    }
    session::ClientSettings settings;
    settings.display_name = std::move(*display_name);
    settings.selected_input_id = std::move(*selected_input_id);
    settings.send_level_db = *send_level_db;
    // 子機の入力拡張の前のバージョンのファイルには input_channels がない。読めない値も含め、
    // これまでと同じモノラルとして読み、ほかの設定は引き継ぐ。
    if (const auto channels = read_unsigned(*root, "input_channels");
        channels && *channels >= 1 && *channels <= control_protocol::kMaxInputChannels) {
        settings.input_channels = static_cast<std::uint8_t>(*channels);
    }
    // 返送モニターの前のバージョンのファイルには output_device_id がない。その場合は「出力なし」
    // として読む。値の型が違えば、ほかの項目と同じく壊れたファイルとして扱う。
    if (root->contains("output_device_id")) {
        auto output_device_id = read_optional_string(*root, "output_device_id");
        if (!output_device_id) {
            return std::nullopt;
        }
        settings.output_device_id = std::move(*output_device_id);
    }
    return settings;
}

}  // namespace vocroz::persistence
