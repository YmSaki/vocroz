// SPDX-License-Identifier: AGPL-3.0-only
//
// StagedPackage の実装。ステージング領域の ZIP を読み、Windows 標準の tar.exe で展開する
// （ライブラリを増やさないため。docs/adr/0009-installer-and-updater.md）。
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "vocroz/update/ports/staged_package.h"

namespace vocroz::updater::win {

class TarStagedPackage final : public update::ports::StagedPackage {
public:
    /// @param updates_dir ステージング領域（データフォルダの updates）
    /// @param install_dir 展開先のインストール先
    TarStagedPackage(std::filesystem::path updates_dir, std::filesystem::path install_dir);

    [[nodiscard]] bool read(const ChunkSink& sink) override;
    [[nodiscard]] bool extract(const update::SemanticVersion& version) override;
    void discard() override;

    /// --check が保存した検証済みのマニフェストを読む。
    /// @return ファイルがあればその内容。なければ std::nullopt（適用する更新がない）。
    [[nodiscard]] std::optional<std::string> read_staged_manifest() const;

private:
    std::filesystem::path updates_dir_;
    std::filesystem::path install_dir_;
};

}  // namespace vocroz::updater::win
