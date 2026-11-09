// SPDX-License-Identifier: AGPL-3.0-only
//
// detail/durable_io.h の Win32 の実装。FlushFileBuffers で内容をディスクに届けてから、
// MoveFileExW の MOVEFILE_WRITE_THROUGH で名前の変更がディスクに届くまで待つ。

#include <limits>

#include <windows.h>

#include "detail/durable_io.h"

namespace vocroz::persistence::detail {
namespace {

// HANDLE を閉じ忘れないための RAII ラッパー。
class FileHandle {
public:
    explicit FileHandle(HANDLE handle) noexcept : handle_(handle) {}
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    FileHandle(FileHandle&&) = delete;
    FileHandle& operator=(FileHandle&&) = delete;
    ~FileHandle() {
        if (valid()) {
            CloseHandle(handle_);
        }
    }

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

    [[nodiscard]] bool valid() const noexcept {
        return handle_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE handle_;
};

}  // namespace

common::Result<void, IoError> write_file_durably(const std::filesystem::path& path,
                                                 std::span<const char> bytes) noexcept {
    // 共有を許さない。書き込み中に他のプロセスが途中の内容を読まないようにするため。
    const FileHandle file{CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file.valid()) {
        return common::make_unexpected(IoError::kFailed);
    }
    std::size_t written = 0;
    while (written < bytes.size()) {
        const auto remaining = bytes.size() - written;
        const auto chunk = static_cast<DWORD>(remaining < std::numeric_limits<DWORD>::max()
                                                  ? remaining
                                                  : std::numeric_limits<DWORD>::max());
        DWORD n = 0;
        if (!WriteFile(file.get(), bytes.data() + written, chunk, &n, nullptr)) {
            return common::make_unexpected(IoError::kFailed);
        }
        written += n;
    }
    if (!FlushFileBuffers(file.get())) {
        return common::make_unexpected(IoError::kFailed);
    }
    return {};
}

common::Result<void, IoError> replace_file_durably(const std::filesystem::path& from,
                                                   const std::filesystem::path& to) noexcept {
    // 同じボリューム内の MoveFileExW は置き換え先をアトミックに差し替える。
    if (!MoveFileExW(from.c_str(), to.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return common::make_unexpected(IoError::kFailed);
    }
    return {};
}

}  // namespace vocroz::persistence::detail
