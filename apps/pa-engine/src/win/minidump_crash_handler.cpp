// SPDX-License-Identifier: AGPL-3.0-only
//
// minidump_crash_handler.h の実装。

// clang-format off
#include <windows.h>
#include <dbghelp.h>
// clang-format on

#include "win/minidump_crash_handler.h"

#include <array>
#include <cwchar>
#include <string>
#include <system_error>
#include <vector>

#include "crash_dump_retention.h"
#include "engine_constants.h"

namespace vocroz::engine::win {
namespace {

// フィルターは引数で文脈を受け取れないので、置き場所はここに 1 回だけ書く。
// 書くのは install のときだけで、以降は読むだけ（クラッシュの最中にヒープを使わないよう、
// パスを固定長の配列に先に用意しておく）。
std::array<wchar_t, MAX_PATH> g_crash_dir{};

LONG WINAPI write_minidump(EXCEPTION_POINTERS* exception) {
    SYSTEMTIME now{};
    ::GetLocalTime(&now);
    // 名前の順が作成の順になるよう、日時を先頭に置く（crash_dump_retention.h）。
    std::array<wchar_t, MAX_PATH> path{};
    const int written =
        std::swprintf(path.data(), path.size(), L"%ls\\pa-engine-%04u%02u%02u-%02u%02u%02u-%lu.dmp",
                      g_crash_dir.data(), now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                      now.wSecond, ::GetCurrentProcessId());
    if (written <= 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    HANDLE file = ::CreateFileW(path.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = ::GetCurrentThreadId();
    info.ExceptionPointers = exception;
    info.ClientPointers = FALSE;
    ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), file, MiniDumpWithDataSegs,
                        exception != nullptr ? &info : nullptr, nullptr, nullptr);
    ::CloseHandle(file);
    // ダンプを書いたら OS の既定の処理（プロセスの終了）に任せる。続けて動かすと、
    // 壊れた状態のまま音を出しうる。
    return EXCEPTION_CONTINUE_SEARCH;
}

void remove_stale_dumps(const std::filesystem::path& crash_dir, common::Logger& logger) {
    std::error_code error;
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(crash_dir, error)) {
        if (entry.is_regular_file(error)) {
            const auto name = entry.path().filename().u8string();
            names.emplace_back(name.begin(), name.end());
        }
    }
    for (const auto& name : select_stale_crash_dumps(names, kCrashDumpsKeptAtStartup)) {
        const std::u8string u8name(name.begin(), name.end());
        if (!std::filesystem::remove(crash_dir / std::filesystem::path{u8name}, error)) {
            logger.log(common::LogLevel::kWarning, "古いクラッシュダンプを消せませんでした");
        }
    }
}

}  // namespace

void install_minidump_crash_handler(const std::filesystem::path& crash_dir,
                                    common::Logger& logger) {
    std::error_code error;
    std::filesystem::create_directories(crash_dir, error);
    if (error) {
        logger.log(common::LogLevel::kWarning,
                   "クラッシュダンプのフォルダを作れないため、ダンプを書きません");
        return;
    }
    const std::wstring dir = crash_dir.wstring();
    if (dir.size() >= g_crash_dir.size() - 64) {
        // ファイル名を足すと MAX_PATH を超えるので、書けない。
        logger.log(common::LogLevel::kWarning,
                   "データフォルダのパスが長すぎるため、クラッシュダンプを書きません");
        return;
    }
    remove_stale_dumps(crash_dir, logger);
    std::wmemcpy(g_crash_dir.data(), dir.c_str(), dir.size() + 1);
    // Windows のエラー報告のダイアログを出さない。出すと閉じられるまでプロセスが残り、
    // remote-pa がエンジンの異常終了に気付けない。
    ::SetErrorMode(::GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    ::SetUnhandledExceptionFilter(write_minidump);
}

}  // namespace vocroz::engine::win
