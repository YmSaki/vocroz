// SPDX-License-Identifier: AGPL-3.0-only
//
// file_install_layout.h の実装。

#include "win/file_install_layout.h"

#include <fstream>
#include <sstream>
#include <system_error>

#include <windows.h>

#include "vocroz/update/install_pointer.h"

namespace vocroz::updater::win {

namespace fs = std::filesystem;

FileInstallLayout::FileInstallLayout(fs::path install_dir) : install_dir_(std::move(install_dir)) {}

std::optional<std::string> FileInstallLayout::read_pointer() {
    std::ifstream file(install_dir_ / update::kInstallPointerFileName, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

bool FileInstallLayout::write_pointer(std::string_view text) {
    const fs::path target = install_dir_ / update::kInstallPointerFileName;
    fs::path temporary = target;
    temporary += L".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.flush();
        if (!file) {
            return false;
        }
    }
    // 書き込みを終えた一時ファイルで置き換え、途中まで書かれたポインターを起動スタブに読ませない。
    // MOVEFILE_WRITE_THROUGH で、置き換えがディスクに届くまで待つ。
    if (!::MoveFileExW(temporary.c_str(), target.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
    return true;
}

std::vector<update::SemanticVersion> FileInstallLayout::installed_versions() {
    std::vector<update::SemanticVersion> versions;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(install_dir_, error)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto name = entry.path().filename().u8string();
        if (const auto version = update::parse_version_directory_name(
                std::string_view{reinterpret_cast<const char*>(name.data()), name.size()})) {
            versions.push_back(*version);
        }
    }
    return versions;
}

bool FileInstallLayout::remove_version(const update::SemanticVersion& version) {
    std::error_code error;
    fs::remove_all(install_dir_ / update::version_directory_name(version), error);
    return !error;
}

}  // namespace vocroz::updater::win
