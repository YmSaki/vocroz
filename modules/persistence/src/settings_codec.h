// SPDX-License-Identifier: AGPL-3.0-only
//
// 設定ファイルの JSON と、session の設定の値との変換。設定ファイルは外部入力なので、
// ここを信頼境界にして型と形を検証し、内側（session）には検証済みの値だけを渡す。
// 値の範囲（フェーダーの範囲や名前の長さ）の意味づけは session が持つので、ここでは検査しない。
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "vocroz/session/settings.h"

namespace vocroz::persistence {

/// 親機の設定を JSON の文字列にする。
/// @param settings 設定
/// @return UTF-8 の JSON
[[nodiscard]] std::string encode_host_settings(const session::HostSettings& settings);

/// 親機の設定の JSON を読む。
/// @param text ファイルの内容
/// @return 設定。形式のバージョン、型、必須のフィールドのどれかが合わなければ std::nullopt
[[nodiscard]] std::optional<session::HostSettings> decode_host_settings(std::string_view text);

/// 子機の設定を JSON の文字列にする。
/// @param settings 設定
/// @return UTF-8 の JSON
[[nodiscard]] std::string encode_client_settings(const session::ClientSettings& settings);

/// 子機の設定の JSON を読む。
/// @param text ファイルの内容
/// @return 設定。形式のバージョン、型、必須のフィールドのどれかが合わなければ std::nullopt
[[nodiscard]] std::optional<session::ClientSettings> decode_client_settings(std::string_view text);

}  // namespace vocroz::persistence
