// SPDX-License-Identifier: AGPL-3.0-only
//
// InstallLayout の実装。インストール先のバージョン別フォルダとポインターファイルを
// std::filesystem と Win32 の MoveFileExW で扱う。
#pragma once

#include <filesystem>

#include "vocroz/update/ports/install_layout.h"

namespace vocroz::updater::win {

class FileInstallLayout final : public update::ports::InstallLayout {
public:
    /// @param install_dir インストール先（起動スタブのあるフォルダ）
    explicit FileInstallLayout(std::filesystem::path install_dir);

    [[nodiscard]] std::optional<std::string> read_pointer() override;
    [[nodiscard]] bool write_pointer(std::string_view text) override;
    [[nodiscard]] std::vector<update::SemanticVersion> installed_versions() override;
    bool remove_version(const update::SemanticVersion& version) override;

private:
    std::filesystem::path install_dir_;
};

}  // namespace vocroz::updater::win
