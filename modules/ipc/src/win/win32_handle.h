// SPDX-License-Identifier: AGPL-3.0-only
//
// Win32 の HANDLE を RAII で保持するラッパー（AGENTS.md 6.1 のリソース管理）。
#pragma once

#include <utility>

#include <windows.h>

namespace vocroz::ipc::win {

/// CloseHandle() で閉じる HANDLE の所有者。
/// CreateFileW と CreateNamedPipeW は失敗時に INVALID_HANDLE_VALUE、CreateEventW は nullptr
/// を返すので、 どちらも無効として扱う。
class UniqueHandle {
public:
    UniqueHandle() = default;

    /// @param handle 所有する HANDLE。無効な値でもよい
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}

    ~UniqueHandle() {
        reset();
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    /// 有効な HANDLE を保持していれば true。
    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    /// 保持している HANDLE。所有権は移さない。
    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

    /// 保持している HANDLE を閉じる。
    void reset() noexcept {
        if (valid()) {
            CloseHandle(handle_);
        }
        handle_ = nullptr;
    }

private:
    HANDLE handle_ = nullptr;
};

/// 手動リセットのイベントを作る。OVERLAPPED の完了と停止の通知に使う。
/// @return 作成したイベント。失敗したら無効な UniqueHandle
[[nodiscard]] inline UniqueHandle make_manual_reset_event() noexcept {
    return UniqueHandle{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
}

}  // namespace vocroz::ipc::win
