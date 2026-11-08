// SPDX-License-Identifier: AGPL-3.0-only
//
// win32_process.h の実装。

#include "win/win32_process.h"

#include <thread>

#include <windows.h>

namespace vocroz::updater::win {
namespace {

// RAII でハンドルを閉じる。
class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
    ~UniqueHandle() {
        reset();
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    void reset(HANDLE handle = nullptr) {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
        }
        handle_ = handle;
    }
    [[nodiscard]] HANDLE get() const {
        return handle_;
    }
    [[nodiscard]] HANDLE* out() {
        reset();
        return &handle_;
    }

private:
    HANDLE handle_ = nullptr;
};

// CommandLineToArgvW と同じ規則で 1 つの引数を引用符で囲む。
// パスに空白が含まれても、受け取る側で 1 つの引数として復元されるようにするため。
[[nodiscard]] std::wstring quote_argument(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
        } else {
            quoted.append(backslashes, L'\\');
        }
        backslashes = 0;
        quoted += c;
    }
    quoted.append(backslashes * 2, L'\\');
    quoted += L'"';
    return quoted;
}

[[nodiscard]] std::wstring command_line(const std::filesystem::path& executable,
                                        const std::vector<std::wstring>& args) {
    std::wstring line = quote_argument(executable.wstring());
    for (const auto& arg : args) {
        line += L' ';
        line += quote_argument(arg);
    }
    return line;
}

}  // namespace

std::optional<ProcessResult> run_and_capture(const std::filesystem::path& executable,
                                             const std::vector<std::wstring>& args,
                                             std::chrono::milliseconds timeout) {
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    UniqueHandle read_end;
    UniqueHandle write_end;
    if (!::CreatePipe(read_end.out(), write_end.out(), &inherit, 0)) {
        return std::nullopt;
    }
    // 読み取り側は子プロセスに継承させない。継承させると子の終了後もパイプが閉じない。
    ::SetHandleInformation(read_end.get(), HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_end.get();
    startup.hStdError = write_end.get();
    startup.hStdInput = nullptr;
    PROCESS_INFORMATION info{};
    std::wstring line = command_line(executable, args);
    if (!::CreateProcessW(executable.c_str(), line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                          nullptr, nullptr, &startup, &info)) {
        return std::nullopt;
    }
    UniqueHandle process{info.hProcess};
    UniqueHandle thread{info.hThread};
    write_end.reset();

    // 出力がパイプのバッファを超えると子が書き込みで止まるので、待つ間も別スレッドで読み続ける。
    std::string output;
    std::thread reader([&output, pipe = read_end.get()] {
        char buffer[4096];
        DWORD read = 0;
        while (::ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
            output.append(buffer, read);
        }
    });

    const DWORD wait = ::WaitForSingleObject(process.get(), static_cast<DWORD>(timeout.count()));
    if (wait != WAIT_OBJECT_0) {
        ::TerminateProcess(process.get(), 1);
        ::WaitForSingleObject(process.get(), INFINITE);
    }
    reader.join();
    if (wait != WAIT_OBJECT_0) {
        return std::nullopt;
    }
    DWORD exit_code = 1;
    ::GetExitCodeProcess(process.get(), &exit_code);
    return ProcessResult{exit_code, std::move(output)};
}

bool start_detached(const std::filesystem::path& executable,
                    const std::vector<std::wstring>& args) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    std::wstring line = command_line(executable, args);
    if (!::CreateProcessW(executable.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                          executable.parent_path().c_str(), &startup, &info)) {
        return false;
    }
    ::CloseHandle(info.hThread);
    ::CloseHandle(info.hProcess);
    return true;
}

}  // namespace vocroz::updater::win
