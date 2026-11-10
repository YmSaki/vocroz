// SPDX-License-Identifier: AGPL-3.0-only
//
// plugin_catalog_codec.h の実装。

#include "plugin_catalog_codec.h"

#include <cstdint>
#include <utility>

#include <nlohmann/json.hpp>

#include "persistence_constants.h"

namespace vocroz::persistence {
namespace {

using json = nlohmann::json;
using session::PluginScanStatus;

const char* status_name(PluginScanStatus status) noexcept {
    switch (status) {
        case PluginScanStatus::kScanned:
            return "scanned";
        case PluginScanStatus::kCrashed:
            return "crashed";
        case PluginScanStatus::kTimedOut:
            return "timed_out";
        case PluginScanStatus::kInvalidOutput:
            return "invalid_output";
    }
    return "invalid_output";
}

std::optional<PluginScanStatus> read_status(const std::string& name) {
    for (const auto status : {PluginScanStatus::kScanned, PluginScanStatus::kCrashed,
                              PluginScanStatus::kTimedOut, PluginScanStatus::kInvalidOutput}) {
        if (name == status_name(status)) {
            return status;
        }
    }
    return std::nullopt;
}

json plugin_to_json(const session::PluginDescription& plugin) {
    return {{"name", plugin.name},
            {"vendor", plugin.vendor},
            {"version", plugin.version},
            {"file_path", plugin.file_path},
            {"identifier", plugin.identifier},
            {"is_instrument", plugin.is_instrument},
            {"input_channels", plugin.input_channels},
            {"output_channels", plugin.output_channels}};
}

template <typename T>
std::optional<T> read(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return std::nullopt;
    }
    if constexpr (std::is_same_v<T, std::string>) {
        if (!it->is_string()) {
            return std::nullopt;
        }
    } else if constexpr (std::is_same_v<T, bool>) {
        if (!it->is_boolean()) {
            return std::nullopt;
        }
    } else if constexpr (std::is_signed_v<T>) {
        if (!it->is_number_integer()) {
            return std::nullopt;
        }
    } else {
        if (!it->is_number_unsigned()) {
            return std::nullopt;
        }
    }
    return it->get<T>();
}

std::optional<session::PluginDescription> read_plugin(const json& object) {
    if (!object.is_object()) {
        return std::nullopt;
    }
    auto name = read<std::string>(object, "name");
    auto vendor = read<std::string>(object, "vendor");
    auto version = read<std::string>(object, "version");
    auto file_path = read<std::string>(object, "file_path");
    auto identifier = read<std::string>(object, "identifier");
    const auto is_instrument = read<bool>(object, "is_instrument");
    const auto inputs = read<std::uint64_t>(object, "input_channels");
    const auto outputs = read<std::uint64_t>(object, "output_channels");
    if (!name || !vendor || !version || !file_path || !identifier || !is_instrument || !inputs ||
        !outputs) {
        return std::nullopt;
    }
    session::PluginDescription plugin;
    plugin.name = std::move(*name);
    plugin.vendor = std::move(*vendor);
    plugin.version = std::move(*version);
    plugin.file_path = std::move(*file_path);
    plugin.identifier = std::move(*identifier);
    plugin.is_instrument = *is_instrument;
    plugin.input_channels = static_cast<std::size_t>(*inputs);
    plugin.output_channels = static_cast<std::size_t>(*outputs);
    return plugin;
}

std::optional<session::PluginScanRecord> read_record(const json& object) {
    if (!object.is_object()) {
        return std::nullopt;
    }
    auto path = read<std::string>(object, "path");
    const auto modified = read<std::int64_t>(object, "modified_time");
    const auto size = read<std::uint64_t>(object, "size_bytes");
    const auto status_text = read<std::string>(object, "status");
    const auto plugins = object.find("plugins");
    if (!path || !modified || !size || !status_text || plugins == object.end() ||
        !plugins->is_array()) {
        return std::nullopt;
    }
    const auto status = read_status(*status_text);
    if (!status) {
        return std::nullopt;
    }
    session::PluginScanRecord record;
    record.file = session::PluginFile{std::move(*path), *modified, *size};
    record.status = *status;
    for (const auto& element : *plugins) {
        auto plugin = read_plugin(element);
        if (!plugin) {
            return std::nullopt;
        }
        record.plugins.push_back(std::move(*plugin));
    }
    return record;
}

}  // namespace

std::string encode_plugin_catalog(std::span<const session::PluginScanRecord> records) {
    json list = json::array();
    for (const auto& record : records) {
        json plugins = json::array();
        for (const auto& plugin : record.plugins) {
            plugins.push_back(plugin_to_json(plugin));
        }
        list.push_back({{"path", record.file.path},
                        {"modified_time", record.file.modified_time},
                        {"size_bytes", record.file.size_bytes},
                        {"status", status_name(record.status)},
                        {"plugins", plugins}});
    }
    const json root{{"schema_version", kPluginCatalogSchemaVersion}, {"records", list}};
    return root.dump(2);
}

std::optional<std::vector<session::PluginScanRecord>> decode_plugin_catalog(std::string_view text) {
    const auto root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return std::nullopt;
    }
    const auto version = read<std::int64_t>(root, "schema_version");
    const auto records = root.find("records");
    if (!version || *version != kPluginCatalogSchemaVersion || records == root.end() ||
        !records->is_array()) {
        return std::nullopt;
    }
    std::vector<session::PluginScanRecord> result;
    for (const auto& element : *records) {
        auto record = read_record(element);
        if (!record) {
            return std::nullopt;
        }
        result.push_back(std::move(*record));
    }
    return result;
}

}  // namespace vocroz::persistence
