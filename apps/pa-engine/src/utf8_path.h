// SPDX-License-Identifier: AGPL-3.0-only
//
// session と control-protocol が UTF-8 の文字列で持つパスと、std::filesystem::path の変換。
// Windows では std::string から作る path が既定のコードページ（ANSI）で解釈され、日本語の
// フォルダ名が壊れるので、必ず UTF-8 として変換する。
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace vocroz::engine {

/// UTF-8 の文字列をパスにする。
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] inline std::filesystem::path utf8_path(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

/// パスを UTF-8 の文字列にする。
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] inline std::string path_utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

}  // namespace vocroz::engine
