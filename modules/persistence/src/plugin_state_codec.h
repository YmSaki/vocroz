// SPDX-License-Identifier: AGPL-3.0-only
//
// 挿入のプラグインの状態の JSON と、session の SavedPluginState との変換。
// 状態はプラグイン独自の形式のバイト列なので、Base64 の文字列にして載せる。
// ファイルは外部入力なので、ここで型と形と大きさを検証する（docs/adr/0042-plugin-insert.md）。
#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "vocroz/session/settings.h"

namespace vocroz::persistence {

/// プラグインの状態を JSON の文字列にする。
/// @param states 状態
/// @return UTF-8 の JSON
[[nodiscard]] std::string encode_plugin_states(std::span<const session::SavedPluginState> states);

/// プラグインの状態の JSON を読む。
/// @param text ファイルの内容
/// @return 状態。形式のバージョン、型、必須のフィールド、Base64 のどれかが合わないか、状態が
///         session::kMaxPluginStateBytes より大きければ std::nullopt
[[nodiscard]] std::optional<std::vector<session::SavedPluginState>> decode_plugin_states(
    std::string_view text);

}  // namespace vocroz::persistence
