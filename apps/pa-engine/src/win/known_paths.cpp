// SPDX-License-Identifier: AGPL-3.0-only
//
// known_paths.h の実装。

#include "win/known_paths.h"

#include <shlobj.h>
#include <windows.h>

namespace vocroz::engine::win {

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

}  // namespace vocroz::engine::win
