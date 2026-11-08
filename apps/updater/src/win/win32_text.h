// SPDX-License-Identifier: AGPL-3.0-only
//
// UTF-8 と UTF-16（Win32 のワイド文字列）の変換。pa-updater のアダプターだけで使う。
#pragma once

#include <string>
#include <string_view>

namespace vocroz::updater::win {

/// UTF-8 を UTF-16 に変換する。不正なバイト列は U+FFFD に置き換わる。
[[nodiscard]] std::wstring widen(std::string_view utf8);

/// UTF-16 を UTF-8 に変換する。
[[nodiscard]] std::string narrow(std::wstring_view utf16);

}  // namespace vocroz::updater::win
