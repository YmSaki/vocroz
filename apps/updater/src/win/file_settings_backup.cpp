// SPDX-License-Identifier: AGPL-3.0-only
//
// file_settings_backup.h の実装。

#include "win/file_settings_backup.h"

#include <system_error>

#include "updater_constants.h"

namespace vocroz::updater::win {

namespace fs = std::filesystem;

FileSettingsBackup::FileSettingsBackup(fs::path data_dir) : data_dir_(std::move(data_dir)) {}

bool FileSettingsBackup::backup() {
    const fs::path settings = data_dir_ / kSettingsFileName;
    std::error_code error;
    had_settings_ = fs::exists(settings, error);
    if (error) {
        return false;
    }
    if (!had_settings_) {
        // 初回起動前に更新した場合は、退避するものがない。
        return true;
    }
    const fs::path backup_dir = data_dir_ / kUpdatesDirName;
    fs::create_directories(backup_dir, error);
    if (error) {
        return false;
    }
    return fs::copy_file(settings, backup_dir / kSettingsBackupFileName,
                         fs::copy_options::overwrite_existing, error) &&
           !error;
}

bool FileSettingsBackup::restore() {
    if (!had_settings_) {
        return true;
    }
    std::error_code error;
    return fs::copy_file(data_dir_ / kUpdatesDirName / kSettingsBackupFileName,
                         data_dir_ / kSettingsFileName, fs::copy_options::overwrite_existing,
                         error) &&
           !error;
}

}  // namespace vocroz::updater::win
