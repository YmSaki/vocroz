// SPDX-License-Identifier: AGPL-3.0-only
//
// COM と Win32 のリソースを保持する RAII ラッパー（AGENTS.md 6.1「リソース管理」）。
#pragma once

#include <memory>

#include <combaseapi.h>
#include <windows.h>

namespace vocroz::device::win {

/// 呼び出したスレッドで COM を初期化し、破棄時に解除する。
/// WASAPI のオブジェクトはどのアパートメントからも使えるので MTA で初期化する。
/// JUCE のメッセージスレッドのようにすでに STA で初期化されたスレッドでは、
/// そのまま使えるので初期化も解除もしない。
class ComApartment {
public:
    ComApartment() noexcept;
    ~ComApartment();
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

    /// COM が使える状態なら true を返す。
    [[nodiscard]] bool is_ready() const noexcept {
        return ready_;
    }

private:
    bool ready_ = false;
    bool needs_uninitialize_ = false;
};

/// CoTaskMemAlloc で確保されたメモリ（GetMixFormat、GetId の結果）を CoTaskMemFree で解放する。
struct CoTaskMemDeleter {
    void operator()(void* memory) const noexcept {
        ::CoTaskMemFree(memory);
    }
};

/// CoTaskMemAlloc で確保されたメモリの所有者。
template <typename T>
using CoTaskMemPtr = std::unique_ptr<T, CoTaskMemDeleter>;

/// CloseHandle で閉じるハンドルの所有者。
class UniqueHandle {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~UniqueHandle();
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    /// 保持しているハンドルを返す（所有権は移さない）。
    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

private:
    HANDLE handle_ = nullptr;
};

}  // namespace vocroz::device::win
