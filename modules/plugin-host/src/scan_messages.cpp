// SPDX-License-Identifier: AGPL-3.0-only
//
// scan_messages.h の実装。nlohmann/json は例外を使わない形（parse の第 3 引数を false）で呼ぶ。

#include "vocroz/plugin_host/scan_messages.h"

#include <optional>
#include <utility>

#include <nlohmann/json.hpp>

#include "plugin_host_constants.h"

namespace vocroz::plugin_host {
namespace {

using common::make_unexpected;
using nlohmann::json;

// 文字列のフィールドを読む。読めたら std::nullopt を返す。
// ないか、型が違うか、長すぎれば、その理由を返す。
std::optional<ScanMessageError> read_string(const json& object, const char* key, std::string& out) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return ScanMessageError::kMissingField;
    }
    const auto& value = it->get_ref<const std::string&>();
    if (value.size() > kMaxStringBytes) {
        return ScanMessageError::kInvalidValue;
    }
    out = value;
    return std::nullopt;
}

// チャンネル数のフィールドを読む。読めたら std::nullopt、読めなければその理由を返す。
std::optional<ScanMessageError> read_channels(const json& object, const char* key,
                                              std::size_t& out) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_unsigned()) {
        return ScanMessageError::kMissingField;
    }
    const auto value = it->get<std::uint64_t>();
    if (value > kMaxReportedChannels) {
        return ScanMessageError::kInvalidValue;
    }
    out = static_cast<std::size_t>(value);
    return std::nullopt;
}

// プラグイン 1 つの記述を読む。読めたら std::nullopt、読めなければその理由を返す。
std::optional<ScanMessageError> read_plugin(const json& object, PluginDescription& out) {
    if (!object.is_object()) {
        return ScanMessageError::kMissingField;
    }
    for (const auto& [key, field] :
         {std::pair{"name", &out.name}, std::pair{"vendor", &out.vendor},
          std::pair{"version", &out.version}, std::pair{"file_path", &out.file_path},
          std::pair{"identifier", &out.identifier}}) {
        if (const auto error = read_string(object, key, *field)) {
            return error;
        }
    }
    // 識別子が空だと、読み込むときにファイル内のプラグインを特定できない。
    if (out.identifier.empty() || out.file_path.empty()) {
        return ScanMessageError::kInvalidValue;
    }
    const auto instrument = object.find("is_instrument");
    if (instrument == object.end() || !instrument->is_boolean()) {
        return ScanMessageError::kMissingField;
    }
    out.is_instrument = instrument->get<bool>();
    if (const auto error = read_channels(object, "input_channels", out.input_channels)) {
        return error;
    }
    return read_channels(object, "output_channels", out.output_channels);
}

}  // namespace

std::string serialize_scan_report(const ScanReport& report) {
    json plugins = json::array();
    for (const PluginDescription& plugin : report.plugins) {
        plugins.push_back({{"name", plugin.name},
                           {"vendor", plugin.vendor},
                           {"version", plugin.version},
                           {"file_path", plugin.file_path},
                           {"identifier", plugin.identifier},
                           {"is_instrument", plugin.is_instrument},
                           {"input_channels", plugin.input_channels},
                           {"output_channels", plugin.output_channels}});
    }
    const json root = {{"version", kScanReportVersion}, {"plugins", std::move(plugins)}};
    // 不正な UTF-8 の名前を報告するプラグインがあっても出力が壊れないよう、置換して書く。
    return root.dump(-1, ' ', false, json::error_handler_t::replace);
}

common::Result<ScanReport, ScanMessageError> parse_scan_report(std::string_view text) {
    const json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return make_unexpected(ScanMessageError::kMalformedJson);
    }
    const auto version = root.find("version");
    if (version == root.end() || !version->is_number_integer()) {
        return make_unexpected(ScanMessageError::kMissingField);
    }
    if (version->get<std::int64_t>() != kScanReportVersion) {
        return make_unexpected(ScanMessageError::kUnsupportedVersion);
    }
    const auto plugins = root.find("plugins");
    if (plugins == root.end() || !plugins->is_array()) {
        return make_unexpected(ScanMessageError::kMissingField);
    }
    if (plugins->size() > kMaxPluginsPerFile) {
        return make_unexpected(ScanMessageError::kInvalidValue);
    }
    ScanReport report;
    report.plugins.reserve(plugins->size());
    for (const json& entry : *plugins) {
        PluginDescription plugin;
        if (const auto error = read_plugin(entry, plugin)) {
            return make_unexpected(*error);
        }
        report.plugins.push_back(std::move(plugin));
    }
    return report;
}

}  // namespace vocroz::plugin_host
