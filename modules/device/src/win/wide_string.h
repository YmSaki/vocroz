// SPDX-License-Identifier: AGPL-3.0-only
//
// Win32 の UTF-16 文字列と、ポートで使う UTF-8 文字列の変換。
// エンドポイント ID と表示名は WASAPI では UTF-16 で、audio-core のポートでは UTF-8 で持つ。
#pragma once

#include <string>
#include <string_view>

namespace vocroz::device::win {

/// UTF-8 を UTF-16 に変換する。不正なバイト列は置換文字になる。
[[nodiscard]] std::wstring to_wide(std::string_view utf8);

/// UTF-16 を UTF-8 に変換する。
[[nodiscard]] std::string to_utf8(std::wstring_view utf16);

}  // namespace vocroz::device::win
