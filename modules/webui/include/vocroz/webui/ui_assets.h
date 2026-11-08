// SPDX-License-Identifier: AGPL-3.0-only
//
// 実行ファイルに埋め込んだ UI のビルド成果物（ui/dist）のファイル。
// CMake の vocroz_embed_ui_assets()（cmake/EmbedUiAssets.cmake）が、ファイルの一覧と内容を
// この形の配列に変換した C++ のソースを生成する（docs/adr/0020-ipc-pipe-and-webui-bridge.md）。
#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace vocroz::webui {

/// 埋め込んだ 1 ファイル。
struct UiAsset {
    std::string_view path;  ///< ui/dist からの相対パス。区切りは `/`（例: `assets/index.js`）
    std::span<const unsigned char> data;  ///< ファイルの内容
};

/// 埋め込んだファイルの一覧。生成したソースの静的な配列を指すので、プロセスの終了まで有効。
using UiAssetTable = std::span<const UiAsset>;

}  // namespace vocroz::webui
