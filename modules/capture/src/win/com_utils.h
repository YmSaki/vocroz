// SPDX-License-Identifier: AGPL-3.0-only
//
// COM と Win32 のリソースを保持する RAII ラッパーと、HRESULT
// の扱い（AGENTS.md 6.1「リソース管理」）。 device
// モジュールにも同じ形のものがあるが、アダプター同士は依存できない（AGENTS.md 4.1）ので、
// このモジュールで使う分だけを持つ。
#pragma once

#include <memory>
#include <string_view>

#include <combaseapi.h>
#include <windows.h>

#include "vocroz/audio_core/audio_device_error.h"
#include "vocroz/common/ports/logger.h"

namespace vocroz::capture::win {

/// 呼び出したスレッドで COM を初期化し、破棄時に解除する。
/// WASAPI のオブジェクトはどのアパートメントからも使えるので MTA で初期化する。
/// すでに STA で初期化されたスレッドでは、そのまま使えるので初期化も解除もしない。
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

/// CoTaskMemAlloc で確保されたメモリ（GetMixFormat の結果）を CoTaskMemFree で解放する。
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

/// WASAPI の HRESULT を、ポートが返すエラーの原因に分類する。
/// @param hr 失敗した HRESULT
/// @return 原因。分類できなければ kInternal
[[nodiscard]] audio_core::AudioDeviceError to_device_error(HRESULT hr) noexcept;

/// 失敗した HRESULT をログに出す。
/// @param logger  出力先
/// @param level   重要度
/// @param context 何をしていて失敗したか（英語の短い句）
/// @param hr      失敗した HRESULT
/// スレッド安全性: オーディオスレッドのループの外から呼ぶ。
void log_hresult(common::Logger& logger, common::LogLevel level, std::string_view context,
                 HRESULT hr) noexcept;

}  // namespace vocroz::capture::win
