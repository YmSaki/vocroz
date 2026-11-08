// SPDX-License-Identifier: AGPL-3.0-only
//
// HealthChecker の実装。新しい版のフォルダの各実行ファイルを --version で起動し、
// 期待するバージョンの 1 行を返すか確かめる。
#pragma once

#include <filesystem>

#include "vocroz/update/ports/health_checker.h"

namespace vocroz::updater::win {

class ProcessHealthChecker final : public update::ports::HealthChecker {
public:
    /// @param install_dir インストール先
    explicit ProcessHealthChecker(std::filesystem::path install_dir);

    [[nodiscard]] bool check(const update::SemanticVersion& version) override;

private:
    std::filesystem::path install_dir_;
};

}  // namespace vocroz::updater::win
