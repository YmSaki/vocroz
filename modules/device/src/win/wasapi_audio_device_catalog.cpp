// SPDX-License-Identifier: AGPL-3.0-only
//
// wasapi_audio_device_catalog.h の実装。

#include "win/wasapi_audio_device_catalog.h"

#include <string>
#include <utility>

#include <mmdeviceapi.h>
#include <propidl.h>
#include <windows.h>
#include <wrl/client.h>

#include "win/com_utils.h"
#include "win/wasapi_endpoint.h"
#include "win/wide_string.h"

namespace vocroz::device::win {
namespace {

using audio_core::AudioDeviceDirection;
using audio_core::AudioDeviceError;
using audio_core::AudioDeviceInfo;
using Microsoft::WRL::ComPtr;

// PKEY_Device_FriendlyName。functiondiscoverykeys_devpkey.h の定義は INITGUID の扱いに
// 左右されるので、値を直接書く。
constexpr PROPERTYKEY kFriendlyNameKey = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

// PropVariantClear で解放する PROPVARIANT。
class ScopedPropVariant {
public:
    ScopedPropVariant() noexcept {
        PropVariantInit(&value_);
    }
    ~ScopedPropVariant() {
        ::PropVariantClear(&value_);
    }
    ScopedPropVariant(const ScopedPropVariant&) = delete;
    ScopedPropVariant& operator=(const ScopedPropVariant&) = delete;

    PROPVARIANT* get() noexcept {
        return &value_;
    }

private:
    PROPVARIANT value_;
};

EDataFlow to_data_flow(AudioDeviceDirection direction) noexcept {
    switch (direction) {
        case AudioDeviceDirection::kOutput:
            return eRender;
        case AudioDeviceDirection::kInput:
            return eCapture;
    }
    return eRender;
}

// エンドポイント ID を UTF-8 で返す。取得できなければ空文字列。
std::string endpoint_id_of(IMMDevice& device) {
    LPWSTR raw_id = nullptr;
    if (FAILED(device.GetId(&raw_id))) {
        return {};
    }
    const CoTaskMemPtr<wchar_t> id(raw_id);
    return to_utf8(id.get());
}

// 表示名を UTF-8 で返す。取得できなければ空文字列（UI は ID の代わりに既定の文言を出す）。
std::string friendly_name_of(IMMDevice& device) {
    ComPtr<IPropertyStore> properties;
    if (FAILED(device.OpenPropertyStore(STGM_READ, &properties))) {
        return {};
    }
    ScopedPropVariant name;
    if (FAILED(properties->GetValue(kFriendlyNameKey, name.get())) || name.get()->vt != VT_LPWSTR ||
        name.get()->pwszVal == nullptr) {
        return {};
    }
    return to_utf8(name.get()->pwszVal);
}

// OS の既定のデバイスの ID を返す。表示のためだけに使い、開くデバイスの選択には使わない。
std::string default_endpoint_id(IMMDeviceEnumerator& enumerator, EDataFlow flow) {
    ComPtr<IMMDevice> device;
    if (FAILED(enumerator.GetDefaultAudioEndpoint(flow, eConsole, &device))) {
        return {};  // デバイスが 1 台もないときは既定もない
    }
    return endpoint_id_of(*device.Get());
}

}  // namespace

common::Result<std::vector<AudioDeviceInfo>, AudioDeviceError> WasapiAudioDeviceCatalog::list(
    AudioDeviceDirection direction) {
    const ComApartment com;
    if (!com.is_ready()) {
        logger_.log(common::LogLevel::kError, "device: COM is not available");
        return common::make_unexpected(AudioDeviceError::kInternal);
    }
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        log_hresult(logger_, common::LogLevel::kError, "create device enumerator", hr);
        return common::make_unexpected(AudioDeviceError::kInternal);
    }
    const EDataFlow flow = to_data_flow(direction);
    ComPtr<IMMDeviceCollection> collection;
    // 抜かれたデバイスと無効にしたデバイスは選べないので、有効なものだけを列挙する。
    hr = enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection);
    UINT count = 0;
    if (SUCCEEDED(hr)) {
        hr = collection->GetCount(&count);
    }
    if (FAILED(hr)) {
        log_hresult(logger_, common::LogLevel::kError, "enumerate endpoints", hr);
        return common::make_unexpected(AudioDeviceError::kInternal);
    }

    const std::string default_id = default_endpoint_id(*enumerator.Get(), flow);
    std::vector<AudioDeviceInfo> devices;
    devices.reserve(count);
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(index, &device))) {
            continue;  // 列挙の途中で抜かれたデバイスは一覧から外す
        }
        AudioDeviceInfo info;
        info.id = endpoint_id_of(*device.Get());
        if (info.id.empty()) {
            continue;
        }
        info.name = friendly_name_of(*device.Get());
        const auto mix = read_mix_format(*device.Get(), logger_);
        info.channel_count = mix ? mix.value().channels : 0;
        info.is_system_default = info.id == default_id;
        devices.push_back(std::move(info));
    }
    return devices;
}

}  // namespace vocroz::device::win
