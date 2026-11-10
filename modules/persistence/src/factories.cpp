// SPDX-License-Identifier: AGPL-3.0-only
//
// OS によらないアダプターのファクトリー関数の実装。

#include "vocroz/persistence/factories.h"

#include <chrono>
#include <utility>

#include "json_plugin_catalog_store.h"
#include "json_settings_store.h"
#include "persistence_constants.h"
#include "rotating_file_logger.h"

namespace vocroz::persistence {

std::filesystem::path log_directory(const std::filesystem::path& data_dir) {
    return data_dir / kLogsDirectoryName;
}

std::unique_ptr<session::SettingsStore> make_json_settings_store(
    const std::filesystem::path& data_dir, common::Logger& logger) {
    return std::make_unique<JsonSettingsStore>(data_dir, logger);
}

std::unique_ptr<session::PluginCatalogStore> make_json_plugin_catalog_store(
    const std::filesystem::path& data_dir, common::Logger& logger) {
    return std::make_unique<JsonPluginCatalogStore>(data_dir, logger);
}

std::unique_ptr<common::Logger> make_rotating_file_logger(LogOptions options) {
    return std::make_unique<RotatingFileLogger>(std::move(options),
                                                [] { return std::chrono::system_clock::now(); });
}

}  // namespace vocroz::persistence
