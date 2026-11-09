// SPDX-License-Identifier: AGPL-3.0-only
//
// 既定の再生デバイスの切り替わりを、ループバックのスレッドに知らせる。
// ループバックは開いた時点の既定のデバイスを集め続けるので、出演者が Windows の設定で再生先を
// 変えると、アプリ音声が届かなくなる。切り替わりで抜去と同じ扱いにし、ClientCapture
// に開き直させる。
#pragma once

#include <mmdeviceapi.h>
#include <windows.h>

namespace vocroz::capture::win {

/// 既定の再生デバイス（コンソールの役割）が変わったら、イベントを立てる IMMNotificationClient。
/// 寿命は所有者（ループバックのスレッドのスタック）が決め、参照カウントでは破棄しない。
/// IMMDeviceEnumerator
/// は登録したクライアントの参照を数えないので、登録を解除してから破棄すればよい。 スレッド安全性:
/// 通知は OS のスレッドから呼ばれ、イベントを立てるだけにする（通知の中では
/// 登録の解除やデバイスの操作をしてはいけないため）。
class DefaultRenderWatcher final : public IMMNotificationClient {
public:
    /// @param changed 切り替わりで立てるイベント。このオブジェクトより長く生存させる
    explicit DefaultRenderWatcher(HANDLE changed) noexcept : changed_(changed) {}
    DefaultRenderWatcher(const DefaultRenderWatcher&) = delete;
    DefaultRenderWatcher& operator=(const DefaultRenderWatcher&) = delete;

    STDMETHODIMP QueryInterface(REFIID iid, void** object) noexcept override;
    STDMETHODIMP_(ULONG) AddRef() noexcept override {
        return 1;  // 寿命は所有者が決める
    }
    STDMETHODIMP_(ULONG) Release() noexcept override {
        return 1;
    }

    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role,
                                        LPCWSTR /*device_id*/) noexcept override;
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR /*device_id*/, DWORD /*state*/) noexcept override {
        return S_OK;  // 開いているデバイスの抜去は、ストリームの AUDCLNT_E_DEVICE_INVALIDATED
                      // で検知する
    }
    STDMETHODIMP OnDeviceAdded(LPCWSTR /*device_id*/) noexcept override {
        return S_OK;
    }
    STDMETHODIMP OnDeviceRemoved(LPCWSTR /*device_id*/) noexcept override {
        return S_OK;
    }
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR /*device_id*/,
                                        const PROPERTYKEY /*key*/) noexcept override {
        return S_OK;
    }

private:
    HANDLE changed_;
};

}  // namespace vocroz::capture::win
