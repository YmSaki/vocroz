// SPDX-License-Identifier: AGPL-3.0-only
//
// session の PluginCatalogStore を、--data-dir の下の JSON ファイルで実装する。
// 保存は AtomicFile で行う。記録は再スキャンで作り直せるので、壊れていたらバックアップを探さず、
// 記録なしとして返す（docs/adr/0032-plugin-catalog-and-loader.md）。
#pragma once

#include <filesystem>
#include <span>
#include <vector>

#include "vocroz/common/ports/logger.h"
#include "vocroz/session/ports/plugin_catalog_store.h"

#include "atomic_file.h"

namespace vocroz::persistence {

/// スキャンの記録を JSON ファイルに保存する。
/// スレッド安全性: PluginCatalog と同じスレッドからだけ呼ぶ。
class JsonPluginCatalogStore final : public session::PluginCatalogStore {
public:
    /// @param data_dir 記録を置くフォルダ（--data-dir）。なければ最初の保存で作る
    /// @param logger   壊れた記録などを記録する。ライフタイムはこのオブジェクトより長い
    JsonPluginCatalogStore(const std::filesystem::path& data_dir, common::Logger& logger);

    common::Result<std::vector<session::PluginScanRecord>, session::SettingsError> load() override;
    common::Result<void, session::SettingsError> save(
        std::span<const session::PluginScanRecord> records) override;

private:
    AtomicFile file_;
    common::Logger& logger_;
};

}  // namespace vocroz::persistence
