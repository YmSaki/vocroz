// SPDX-License-Identifier: AGPL-3.0-only
//
// 起動スタブが、どのバージョン別フォルダの remote-pa を起動するかの判断。
#pragma once

#include <optional>
#include <vector>

#include "vocroz/update/install_pointer.h"
#include "vocroz/update/semantic_version.h"

namespace vocroz::update {

/// 起動スタブが試すバージョンを、試す順に返す。
/// ポインターの現行版とスタブ自身の版（インストーラーが入れた版）のうち新しい方を先にする。
/// インストーラーで新しい版を上書き導入したとき、古いポインターに引きずられないようにするため。
/// その次にもう一方、最後にポインターの直前の版を試す。重複は除く。
/// @param pointer ポインターファイルの内容。ファイルがないか壊れていれば std::nullopt
/// @param own_version 起動スタブ自身のバージョン
/// @return 試す順のバージョン。少なくとも own_version を含む。スレッド安全。
[[nodiscard]] std::vector<SemanticVersion> launch_candidates(
    const std::optional<InstallPointer>& pointer, const SemanticVersion& own_version);

}  // namespace vocroz::update
