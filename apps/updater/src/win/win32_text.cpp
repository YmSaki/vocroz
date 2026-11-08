// SPDX-License-Identifier: AGPL-3.0-only
//
// win32_text.h の実装。

#include "win/win32_text.h"

#include <windows.h>

namespace vocroz::updater::win {

std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int length =
        ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(),
                          length);
    return result;
}

std::string narrow(std::wstring_view utf16) {
    if (utf16.empty()) {
        return {};
    }
    const int length = ::WideCharToMultiByte(
        CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(length), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), result.data(),
                          length, nullptr, nullptr);
    return result;
}

}  // namespace vocroz::updater::win
