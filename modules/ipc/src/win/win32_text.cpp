// SPDX-License-Identifier: AGPL-3.0-only
//
// UTF-8 と UTF-16 の変換。MultiByteToWideChar と WideCharToMultiByte を使う。

#include "win/win32_text.h"

#include <windows.h>

namespace vocroz::ipc::win {

std::wstring to_wide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int input_size = static_cast<int>(utf8.size());
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), input_size, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), input_size, wide.data(), size);
    return wide;
}

std::string to_utf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int input_size = static_cast<int>(wide.size());
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), input_size, nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), input_size, utf8.data(), size, nullptr, nullptr);
    return utf8;
}

}  // namespace vocroz::ipc::win
