// SPDX-License-Identifier: AGPL-3.0-only
//
// JsonPluginCatalogStore の実装。

#include "json_plugin_catalog_store.h"

#include <string>

#include "persistence_constants.h"
#include "plugin_catalog_codec.h"

namespace vocroz::persistence {

using session::SettingsError;

JsonPluginCatalogStore::JsonPluginCatalogStore(const std::filesystem::path& data_dir,
                                               common::Logger& logger)
    : file_(data_dir / kPluginCatalogFileName), logger_(logger) {}

common::Result<std::vector<session::PluginScanRecord>, SettingsError>
JsonPluginCatalogStore::load() {
    const auto current = file_.read_current();
    if (!current) {
        return common::make_unexpected(current.error() == AtomicFileError::kIoError
                                           ? SettingsError::kIoError
                                           : SettingsError::kCorrupted);
    }
    if (!current.value()) {
        return common::make_unexpected(SettingsError::kNotFound);
    }
    auto records = decode_plugin_catalog(*current.value());
    if (!records) {
        logger_.log(common::LogLevel::kWarning,
                    "プラグインのスキャンの記録を読めないため、すべてを再スキャンします");
        return common::make_unexpected(SettingsError::kCorrupted);
    }
    return std::move(*records);
}

common::Result<void, SettingsError> JsonPluginCatalogStore::save(
    std::span<const session::PluginScanRecord> records) {
    // 記録は作り直せるので、バックアップは残さない。
    if (!file_.commit(encode_plugin_catalog(records), false)) {
        logger_.log(common::LogLevel::kWarning, "プラグインのスキャンの記録を保存できませんでした");
        return common::make_unexpected(SettingsError::kIoError);
    }
    return {};
}

}  // namespace vocroz::persistence
