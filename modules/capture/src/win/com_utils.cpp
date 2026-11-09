// SPDX-License-Identifier: AGPL-3.0-only
//
// com_utils.h の実装。

#include "win/com_utils.h"

#include <cstdio>

#include <audioclient.h>

namespace vocroz::capture::win {
namespace {

using audio_core::AudioDeviceError;

// HRESULT_FROM_WIN32(ERROR_NOT_FOUND)。HRESULT_FROM_WIN32 は定数式にならないので値を直接書く。
constexpr HRESULT kNotFound = static_cast<HRESULT>(0x80070490L);

}  // namespace

ComApartment::ComApartment() noexcept {
    const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        // S_FALSE（すでに同じモードで初期化済み）でも、呼び出しごとに解除が要る。
        ready_ = true;
        needs_uninitialize_ = true;
    } else if (hr == RPC_E_CHANGED_MODE) {
        ready_ = true;
    }
}

ComApartment::~ComApartment() {
    if (needs_uninitialize_) {
        ::CoUninitialize();
    }
}

UniqueHandle::~UniqueHandle() {
    if (handle_ != nullptr) {
        ::CloseHandle(handle_);
    }
}

AudioDeviceError to_device_error(HRESULT hr) noexcept {
    if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == kNotFound) {
        return AudioDeviceError::kDeviceNotFound;
    }
    if (hr == AUDCLNT_E_DEVICE_IN_USE) {
        return AudioDeviceError::kDeviceBusy;
    }
    if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT) {
        return AudioDeviceError::kUnsupportedFormat;
    }
    return AudioDeviceError::kInternal;
}

void log_hresult(common::Logger& logger, common::LogLevel level, std::string_view context,
                 HRESULT hr) noexcept {
    char text[160];
    std::snprintf(text, sizeof(text), "capture: %.*s failed (hr=0x%08lX)",
                  static_cast<int>(context.size()), context.data(), static_cast<unsigned long>(hr));
    logger.log(level, text);
}

}  // namespace vocroz::capture::win
