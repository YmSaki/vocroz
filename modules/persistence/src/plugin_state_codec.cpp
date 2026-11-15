// SPDX-License-Identifier: AGPL-3.0-only
//
// plugin_state_codec.h の実装。

#include "plugin_state_codec.h"

#include <cstdint>
#include <utility>

#include <nlohmann/json.hpp>

#include "detail/base64_utils.h"
#include "persistence_constants.h"

namespace vocroz::persistence {
namespace {

using nlohmann::json;

constexpr const char* kSchemaVersionKey = "schema_version";
constexpr const char* kStatesKey = "plugin_states";

std::optional<std::string> read_string(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

std::optional<session::SavedPluginState> read_state(const json& element) {
    if (!element.is_object()) {
        return std::nullopt;
    }
    auto insert_id = read_string(element, "insert_id");
    auto plugin_id = read_string(element, "plugin_id");
    const auto encoded = read_string(element, "state");
    if (!insert_id || insert_id->empty() || !plugin_id || plugin_id->empty() || !encoded) {
        return std::nullopt;
    }
    auto bytes = detail::decode_base64(*encoded);
    if (!bytes || bytes->size() > session::kMaxPluginStateBytes) {
        return std::nullopt;
    }
    return session::SavedPluginState{std::move(*insert_id), std::move(*plugin_id),
                                     std::move(*bytes)};
}

}  // namespace

std::string encode_plugin_states(std::span<const session::SavedPluginState> states) {
    json root = json::object();
    root[kSchemaVersionKey] = kPluginStatesSchemaVersion;
    json array = json::array();
    for (const auto& state : states) {
        array.push_back({{"insert_id", state.insert_id},
                         {"plugin_id", state.plugin_id},
                         {"state", detail::encode_base64(state.state)}});
    }
    root[kStatesKey] = std::move(array);
    // 識別子に不正な UTF-8 が入っていても保存は止めず、置換文字にして書く（設定と同じ）。
    // 状態は大きいことがあるので、字下げせずに書く。
    return root.dump(-1, ' ', false, json::error_handler_t::replace) + "\n";
}

std::optional<std::vector<session::SavedPluginState>> decode_plugin_states(std::string_view text) {
    const auto root = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (!root.is_object()) {
        return std::nullopt;
    }
    const auto version = root.find(kSchemaVersionKey);
    if (version == root.end() || !version->is_number_integer() ||
        version->get<std::int64_t>() != kPluginStatesSchemaVersion) {
        return std::nullopt;
    }
    const auto array = root.find(kStatesKey);
    if (array == root.end() || !array->is_array()) {
        return std::nullopt;
    }
    std::vector<session::SavedPluginState> states;
    states.reserve(array->size());
    for (const auto& element : *array) {
        auto state = read_state(element);
        if (!state) {
            return std::nullopt;
        }
        states.push_back(std::move(*state));
    }
    return states;
}

}  // namespace vocroz::persistence
