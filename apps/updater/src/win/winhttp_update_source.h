// SPDX-License-Identifier: AGPL-3.0-only
//
// UpdateSource の実装。WinHTTP で HTTPS だけを使い、マニフェストとパッケージを取得する。
#pragma once

#include <filesystem>
#include <string>

#include "vocroz/update/ports/update_source.h"

namespace vocroz::updater::win {

class WinHttpUpdateSource final : public update::ports::UpdateSource {
public:
    /// @param manifest_url マニフェストの HTTPS の URL
    /// @param updates_dir ステージング領域（データフォルダの updates）
    WinHttpUpdateSource(std::string manifest_url, std::filesystem::path updates_dir);

    [[nodiscard]] std::optional<std::string> fetch_manifest() override;
    [[nodiscard]] bool download_package(const update::UpdateManifest& manifest) override;
    [[nodiscard]] bool store_manifest(std::string_view text) override;

private:
    std::string manifest_url_;
    std::filesystem::path updates_dir_;
};

}  // namespace vocroz::updater::win
