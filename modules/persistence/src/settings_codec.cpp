// SPDX-License-Identifier: AGPL-3.0-only
//
// 設定ファイルの JSON の変換の実装。
// nlohmann/json は例外を送出しない使い方（parse の第 3 引数を false）にして、
// 壊れたファイルを例外ではなく std::nullopt として扱う。

#include "settings_codec.h"

#include <nlohmann/json.hpp>

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
    root["master_gain_db"] =
        settings.master_gain_db ? json(*settings.master_gain_db) : json(nullptr);
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
    return settings;
}

std::string encode_client_settings(const session::ClientSettings& settings) {
    json root = json::object();
    root[kSchemaVersionKey] = kSettingsSchemaVersion;
    root["display_name"] = settings.display_name;
    root["selected_input_id"] = optional_to_json(settings.selected_input_id);
    root["send_level_db"] = settings.send_level_db;
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
    return settings;
}

}  // namespace vocroz::persistence
