// SPDX-License-Identifier: AGPL-3.0-only
//
// default_render_watcher.h の実装。

#include "win/default_render_watcher.h"

namespace vocroz::capture::win {

STDMETHODIMP DefaultRenderWatcher::QueryInterface(REFIID iid, void** object) noexcept {
    if (object == nullptr) {
        return E_POINTER;
    }
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
        *object = static_cast<IMMNotificationClient*>(this);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP DefaultRenderWatcher::OnDefaultDeviceChanged(EDataFlow flow, ERole role,
                                                          LPCWSTR /*device_id*/) noexcept {
    // ループバックはコンソールの役割の既定の再生デバイスを開くので、それ以外の変化は無視する。
    if (flow == eRender && role == eConsole) {
        ::SetEvent(changed_);
    }
    return S_OK;
}

}  // namespace vocroz::capture::win
