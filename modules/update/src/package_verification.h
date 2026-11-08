// SPDX-License-Identifier: AGPL-3.0-only
//
// ダウンロードしたパッケージがマニフェストと一致するかの検証。
// 確認（--check）と適用（--apply）の両方が、同じ規則で検証するために 1 か所にまとめる。
#pragma once

#include "vocroz/update/ports/staged_package.h"
#include "vocroz/update/update_manifest.h"

namespace vocroz::update {

/// パッケージのサイズと SHA-256 がマニフェストと一致するか。
/// @return 最後まで読めて、サイズとダイジェストが一致すれば true。コントロールスレッドから呼ぶ。
[[nodiscard]] bool package_matches_manifest(ports::StagedPackage& package,
                                            const UpdateManifest& manifest);

}  // namespace vocroz::update
