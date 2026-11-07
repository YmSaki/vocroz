// SPDX-License-Identifier: AGPL-3.0-only
//
// SettingsBackup の実装。データフォルダの settings.json を updates に複製し、ロールバックで戻す。
#pragma once

#include <filesystem>

#include "vocroz/update/ports/settings_backup.h"

namespace vocroz::updater::win {

class FileSettingsBackup final : public update::ports::SettingsBackup {
public:
    /// @param data_dir データフォルダ
    explicit FileSettingsBackup(std::filesystem::path data_dir);

    [[nodiscard]] bool backup() override;
    [[nodiscard]] bool restore() override;

private:
    std::filesystem::path data_dir_;
    bool had_settings_ = false;
};

}  // namespace vocroz::updater::win
