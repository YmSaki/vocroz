// SPDX-License-Identifier: AGPL-3.0-only
//
// win32_launch.h の実装。

#include "win/win32_launch.h"

#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

#include <windows.h>

namespace vocroz::launcher::win {

std::optional<std::filesystem::path> own_directory() {
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

std::optional<std::string> read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

bool launch(const std::filesystem::path& executable, std::wstring_view arguments) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(executable, error)) {
        return false;
    }
    // 子のコマンドラインは、子自身のパスを先頭にし、受け取った引数を手を加えずに続ける。
    std::wstring line = L"\"" + executable.wstring() + L"\"";
    if (!arguments.empty()) {
        line += L' ';
        line += arguments;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    // 作業フォルダはバージョン別フォルダにし、相対パスの DLL 探索が別の版を拾わないようにする。
    const std::wstring working_dir = executable.parent_path().wstring();
    if (!::CreateProcessW(executable.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                          working_dir.c_str(), &startup, &info)) {
        return false;
    }
    ::CloseHandle(info.hThread);
    ::CloseHandle(info.hProcess);
    return true;
}

void show_error(std::wstring_view message) {
    const std::wstring text{message};
    ::MessageBoxW(nullptr, text.c_str(), L"VOCROZ", MB_OK | MB_ICONERROR);
}

}  // namespace vocroz::launcher::win
