// SPDX-License-Identifier: AGPL-3.0-only
//
// WebView のリソースの要求に、埋め込んだ UI のファイルで答える。
// JUCE のリソースプロバイダーから呼ぶが、JUCE に依存しない純粋な関数にして Linux の CI
// でも検証する。
#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "vocroz/webui/ui_assets.h"

namespace vocroz::webui {

/// 要求に答えるファイルの内容と MIME タイプ。
struct UiResource {
    std::span<const unsigned char> data;
    std::string_view mime_type;
};

/// 要求されたパスに対応するファイルを探す。
/// `/` は `index.html` に読み替え、クエリ（`?`）とフラグメント（`#`）は無視する。
/// `..` や `\` を含むパスは、埋め込んだファイルの外を指しうるので拒否する。
/// @param assets       埋め込んだファイルの一覧
/// @param request_path リソースプロバイダーのルートからのパス（例: `/assets/index.js?v=1`）
/// @return ファイルの内容と MIME タイプ。対応するファイルがないか、拒否したら std::nullopt
/// スレッド安全性: 状態を持たないので、どのスレッドからも同時に呼び出し可能。
[[nodiscard]] std::optional<UiResource> find_ui_resource(UiAssetTable assets,
                                                         std::string_view request_path);

/// 拡張子から MIME タイプを決める。
/// @param path ファイルのパス
/// @return MIME タイプ。知らない拡張子は `application/octet-stream`
/// スレッド安全性: 状態を持たないので、どのスレッドからも同時に呼び出し可能。
[[nodiscard]] std::string_view mime_type_for(std::string_view path);

}  // namespace vocroz::webui
