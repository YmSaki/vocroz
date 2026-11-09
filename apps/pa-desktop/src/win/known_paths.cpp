// SPDX-License-Identifier: AGPL-3.0-only
//
// known_paths.h の実装。

#include "win/known_paths.h"

// clang-format off
#include <windows.h>
#include <shlobj.h>
// clang-format on

#include <string>
#include <vector>

namespace vocroz::pa_desktop::win {

std::optional<std::filesystem::path> default_data_dir() {
    PWSTR path = nullptr;
    if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        ::CoTaskMemFree(path);
        return std::nullopt;
    }
    std::filesystem::path result{path};
    ::CoTaskMemFree(path);
    return result / L"VOCROZ";
}

std::optional<std::filesystem::path> executable_directory() {
    // 長いパスにも対応できるよう、足りなければバッファを広げて取り直す。
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true) {
        const DWORD length =
            ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return std::nullopt;
        }
        if (length < buffer.size()) {
            return std::filesystem::path{std::wstring{buffer.data(), length}}.parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
}

}  // namespace vocroz::pa_desktop::win
