// SPDX-License-Identifier: AGPL-3.0-only
//
// インストール先（バージョン別フォルダとポインターファイル）のポート。実装は
// apps/updater/src/win/。
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vocroz/update/semantic_version.h"

namespace vocroz::update::ports {

/// インストール先のフォルダ構成。
class InstallLayout {
public:
    virtual ~InstallLayout() = default;

    /// ポインターファイルのテキストを読む。
    /// @return ファイルがあればその内容。なければ std::nullopt。コントロールスレッドから呼ぶ。
    [[nodiscard]] virtual std::optional<std::string> read_pointer() = 0;

    /// ポインターファイルを置き換える。一時ファイルに書いてから置き換え、途中の状態を残さない。
    /// @return 置き換えられたら true。失敗したら元のファイルのまま false。
    [[nodiscard]] virtual bool write_pointer(std::string_view text) = 0;

    /// インストール先にあるバージョン別フォルダのバージョンを返す（順不同）。
    [[nodiscard]] virtual std::vector<SemanticVersion> installed_versions() = 0;

    /// バージョン別フォルダを削除する。古い版の掃除に使い、失敗しても更新は成功として扱う。
    /// @return 削除できたら true。
    virtual bool remove_version(const SemanticVersion& version) = 0;
};

}  // namespace vocroz::update::ports
