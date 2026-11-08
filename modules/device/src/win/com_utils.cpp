// SPDX-License-Identifier: AGPL-3.0-only
//
// com_utils.h の実装。

#include "win/com_utils.h"

namespace vocroz::device::win {

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

}  // namespace vocroz::device::win
