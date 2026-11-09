// SPDX-License-Identifier: AGPL-3.0-only
//
// win32_engine_launcher.h の実装。

#include "win/win32_engine_launcher.h"

#include <system_error>
#include <utility>

#include <windows.h>

#include "command_line.h"

namespace vocroz::pa_desktop::win {

Win32EngineLauncher::Win32EngineLauncher(std::filesystem::path executable,
                                         const std::filesystem::path& data_dir,
                                         std::string_view log_level, common::Logger& logger)
    : executable_(std::move(executable)), logger_(logger) {
    // 子のコマンドラインは、子自身のパスを先頭にし、pa-engine が受け付ける 2 つの引数だけを渡す
    // （docs/adr/0007-launch-interface.md）。パイプの名前はデータフォルダから導くので渡さない。
    command_line_ = quote_argument(executable_.wstring());
    command_line_ += L' ';
    command_line_ += quote_argument(L"--data-dir=" + data_dir.wstring());
    command_line_ += L' ';
    command_line_ +=
        quote_argument(L"--log-level=" + std::wstring(log_level.begin(), log_level.end()));
}

Win32EngineLauncher::~Win32EngineLauncher() {
    // ハンドルを閉じるだけで、エンジンは止めない。remote-pa を閉じても音を止めないため。
    close_process();
}

bool Win32EngineLauncher::launch() {
    close_process();
    std::error_code error;
    if (!std::filesystem::is_regular_file(executable_, error)) {
        logger_.log(common::LogLevel::kError, "pa-engine の実行ファイルが見つかりません");
        return false;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    std::wstring line = command_line_;
    // 作業フォルダはバージョン別フォルダにし、相対パスの DLL
    // 探索が別のバージョンを拾わないようにする。
    // コンソールは作らない（エンジンは画面を持たず、ログはデータフォルダに書く）。
    const std::wstring working_dir = executable_.parent_path().wstring();
    if (!::CreateProcessW(executable_.c_str(), line.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, working_dir.c_str(), &startup, &info)) {
        logger_.log(common::LogLevel::kError, "pa-engine を起動できません");
        return false;
    }
    ::CloseHandle(info.hThread);
    process_ = info.hProcess;
    logger_.log(common::LogLevel::kInfo, "pa-engine を起動しました");
    return true;
}

std::optional<int> Win32EngineLauncher::exit_code() {
    if (process_ == nullptr) {
        return std::nullopt;
    }
    DWORD code = 0;
    if (!::GetExitCodeProcess(static_cast<HANDLE>(process_), &code) || code == STILL_ACTIVE) {
        return std::nullopt;
    }
    return static_cast<int>(code);
}

void Win32EngineLauncher::close_process() noexcept {
    if (process_ != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(process_));
        process_ = nullptr;
    }
}

}  // namespace vocroz::pa_desktop::win
