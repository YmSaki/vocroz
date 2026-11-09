// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の PluginCatalogStore。メモリに記録を持ち、読み込みの失敗も再現できる。
#pragma once

#include <optional>
#include <vector>

#include "vocroz/session/ports/plugin_catalog_store.h"

namespace vocroz::session {

/// メモリに記録を持つ PluginCatalogStore。記録がなければ kNotFound を返す。
class FakePluginCatalogStore final : public PluginCatalogStore {
public:
    common::Result<std::vector<PluginScanRecord>, SettingsError> load() override {
        if (load_error_) {
            return common::make_unexpected(*load_error_);
        }
        if (!records_) {
            return common::make_unexpected(SettingsError::kNotFound);
        }
        return *records_;
    }

    common::Result<void, SettingsError> save(std::span<const PluginScanRecord> records) override {
        records_ = std::vector<PluginScanRecord>(records.begin(), records.end());
        ++save_count_;
        return {};
    }

    /// 読み込みを error で失敗させる（壊れた記録の再現）。
    void fail_loads_with(SettingsError error) noexcept {
        load_error_ = error;
    }

    /// @return 保存した記録。保存していなければ std::nullopt
    [[nodiscard]] const std::optional<std::vector<PluginScanRecord>>& records() const noexcept {
        return records_;
    }

    /// 保存した記録を置き換える（前回の起動で保存した記録の再現）。
    void set_records(std::vector<PluginScanRecord> records) {
        records_ = std::move(records);
    }

    /// @return save() を呼んだ回数
    [[nodiscard]] int save_count() const noexcept {
        return save_count_;
    }

private:
    std::optional<std::vector<PluginScanRecord>> records_;
    std::optional<SettingsError> load_error_;
    int save_count_ = 0;
};

}  // namespace vocroz::session
