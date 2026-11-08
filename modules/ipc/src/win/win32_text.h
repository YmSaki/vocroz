// SPDX-License-Identifier: AGPL-3.0-only
//
// UTF-8 と UTF-16（Win32 のワイド文字列）の変換。
#pragma once

#include <string>
#include <string_view>

namespace vocroz::ipc::win {

/// UTF-8 を UTF-16 に変換する。不正なバイト列は U+FFFD に置き換える。
[[nodiscard]] std::wstring to_wide(std::string_view utf8);

/// UTF-16 を UTF-8 に変換する。不正なサロゲートは U+FFFD に置き換える。
[[nodiscard]] std::string to_utf8(std::wstring_view wide);

}  // namespace vocroz::ipc::win
