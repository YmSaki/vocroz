// SPDX-License-Identifier: AGPL-3.0-only
//
// known_paths.h の実装。

#include "win/known_paths.h"

#include <string>

#include <shlobj.h>
#include <windows.h>

namespace vocroz::engine::win {
namespace {

[[nodiscard]] std::optional<std::filesystem::path> known_folder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    if (FAILED(::SHGetKnownFolderPath(id, 0, nullptr, &path))) {
        ::CoTaskMemFree(path);
        return std::nullopt;
    }
    std::filesystem::path result{path};
    ::CoTaskMemFree(path);
    return result;
}

}  // namespace

std::optional<std::filesystem::path> default_data_dir() {
    const auto local_app_data = known_folder(FOLDERID_LocalAppData);
    if (!local_app_data) {
        return std::nullopt;
    }
    return *local_app_data / L"VOCROZ";
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

std::vector<std::filesystem::path> vst3_directories() {
    std::vector<std::filesystem::path> directories;
    // 64 ビットのプロセスなので、FOLDERID_ProgramFilesCommon は 64 ビット版の Common Files を指す。
    if (const auto common = known_folder(FOLDERID_ProgramFilesCommon)) {
        directories.push_back(*common / L"VST3");
    }
    if (const auto user_common = known_folder(FOLDERID_UserProgramFilesCommon)) {
        directories.push_back(*user_common / L"VST3");
    }
    return directories;
}

}  // namespace vocroz::engine::win
