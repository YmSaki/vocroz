// SPDX-License-Identifier: AGPL-3.0-only
//
// win32_named_mutex.h の実装。

#include "win/win32_named_mutex.h"

#include <utility>

#include <windows.h>

namespace vocroz::engine::win {

common::Result<Win32NamedMutex, NamedMutexError> Win32NamedMutex::create(const std::wstring& name,
                                                                         bool exclusive) {
    // 所有はしない（bInitialOwner = FALSE）。待ち合わせには使わず、存在だけを印にするため。
    HANDLE handle = ::CreateMutexW(nullptr, FALSE, name.c_str());
    if (handle == nullptr) {
        // 別の種類のオブジェクトが同じ名前を持っている場合も、ここに来る。
        return common::make_unexpected(::GetLastError() == ERROR_ACCESS_DENIED
                                           ? NamedMutexError::kAlreadyExists
                                           : NamedMutexError::kSystemError);
    }
    if (exclusive && ::GetLastError() == ERROR_ALREADY_EXISTS) {
        ::CloseHandle(handle);
        return common::make_unexpected(NamedMutexError::kAlreadyExists);
    }
    return Win32NamedMutex{handle};
}

Win32NamedMutex::Win32NamedMutex(Win32NamedMutex&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)) {}

Win32NamedMutex& Win32NamedMutex::operator=(Win32NamedMutex&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

Win32NamedMutex::~Win32NamedMutex() {
    close();
}

void Win32NamedMutex::close() noexcept {
    if (handle_ != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
}

}  // namespace vocroz::engine::win
