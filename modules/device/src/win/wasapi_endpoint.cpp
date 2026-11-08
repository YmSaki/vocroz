// SPDX-License-Identifier: AGPL-3.0-only
//
// wasapi_endpoint.h の実装。

#include "win/wasapi_endpoint.h"

#include <cstdio>
#include <string>

#include "vocroz/audio_core/constants.h"

#include "win/com_utils.h"
#include "win/wide_string.h"

namespace vocroz::device::win {
namespace {

using audio_core::AudioDeviceError;
using Microsoft::WRL::ComPtr;

// HRESULT_FROM_WIN32(ERROR_NOT_FOUND)。HRESULT_FROM_WIN32 は定数式にならないので値を直接書く。
constexpr HRESULT kNotFound = static_cast<HRESULT>(0x80070490L);

// 32 ビット浮動小数点のサンプルのビット数。
constexpr WORD kFloat32Bits = 32;

// format が WAVEFORMATEXTENSIBLE なら、その拡張部分を返す。そうでなければ nullptr。
// 拡張部分の長さ（cbSize）を確かめてから読み、ドライバーの不正な値で範囲外を読まないようにする。
const WAVEFORMATEXTENSIBLE* as_extensible(const WAVEFORMATEX& format) noexcept {
    if (format.wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
        format.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        return nullptr;
    }
    return reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(&format);
}

}  // namespace

AudioDeviceError to_device_error(HRESULT hr) noexcept {
    if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == kNotFound) {
        return AudioDeviceError::kDeviceNotFound;
    }
    if (hr == AUDCLNT_E_DEVICE_IN_USE || hr == AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED) {
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
    std::snprintf(text, sizeof(text), "device: %.*s failed (hr=0x%08lX)",
                  static_cast<int>(context.size()), context.data(), static_cast<unsigned long>(hr));
    logger.log(level, text);
}

common::Result<ComPtr<IMMDevice>, AudioDeviceError> open_endpoint(std::string_view endpoint_id,
                                                                  EDataFlow flow,
                                                                  common::Logger& logger) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        log_hresult(logger, common::LogLevel::kError, "create device enumerator", hr);
        return common::make_unexpected(AudioDeviceError::kInternal);
    }
    // 空や不正な ID も「その ID のデバイスはない」として扱い、既定のデバイスに置き換えない。
    const std::wstring id = to_wide(endpoint_id);
    ComPtr<IMMDevice> device;
    hr = id.empty() ? E_INVALIDARG : enumerator->GetDevice(id.c_str(), &device);
    if (FAILED(hr)) {
        log_hresult(logger, common::LogLevel::kWarning, "resolve endpoint id", hr);
        return common::make_unexpected(AudioDeviceError::kDeviceNotFound);
    }
    // GetDevice は抜かれたデバイスや無効にしたデバイスも返すので、状態を確かめる。
    DWORD state = 0;
    if (FAILED(device->GetState(&state)) || state != DEVICE_STATE_ACTIVE) {
        logger.log(common::LogLevel::kWarning, "device: endpoint is not active");
        return common::make_unexpected(AudioDeviceError::kDeviceNotFound);
    }
    ComPtr<IMMEndpoint> endpoint;
    EDataFlow actual_flow = eAll;
    if (FAILED(device.As(&endpoint)) || FAILED(endpoint->GetDataFlow(&actual_flow)) ||
        actual_flow != flow) {
        logger.log(common::LogLevel::kWarning, "device: endpoint direction does not match");
        return common::make_unexpected(AudioDeviceError::kDeviceNotFound);
    }
    return device;
}

detail::MixFormat summarize_format(const WAVEFORMATEX& format) noexcept {
    bool is_float = format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (const WAVEFORMATEXTENSIBLE* extensible = as_extensible(format)) {
        is_float = extensible->SubFormat == kSubtypeIeeeFloat;
    }
    return detail::MixFormat{format.nSamplesPerSec, format.nChannels,
                             is_float && format.wBitsPerSample == kFloat32Bits};
}

DWORD channel_mask_of(const WAVEFORMATEX& format) noexcept {
    const WAVEFORMATEXTENSIBLE* extensible = as_extensible(format);
    return extensible != nullptr ? extensible->dwChannelMask : 0;
}

WAVEFORMATEXTENSIBLE make_internal_format(std::size_t channels, DWORD channel_mask) noexcept {
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = static_cast<WORD>(channels);
    format.Format.nSamplesPerSec = static_cast<DWORD>(audio_core::kSampleRate);
    format.Format.wBitsPerSample = kFloat32Bits;
    format.Format.nBlockAlign = static_cast<WORD>(channels * sizeof(float));
    format.Format.nAvgBytesPerSec = format.Format.nSamplesPerSec * format.Format.nBlockAlign;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = kFloat32Bits;
    format.dwChannelMask = channel_mask;
    format.SubFormat = kSubtypeIeeeFloat;
    return format;
}

common::Result<detail::MixFormat, AudioDeviceError> read_mix_format(IMMDevice& device,
                                                                    common::Logger& logger) {
    ComPtr<IAudioClient> client;
    HRESULT hr = device.Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                 reinterpret_cast<void**>(client.GetAddressOf()));
    if (FAILED(hr)) {
        log_hresult(logger, common::LogLevel::kWarning, "activate audio client", hr);
        return common::make_unexpected(to_device_error(hr));
    }
    WAVEFORMATEX* raw_format = nullptr;
    hr = client->GetMixFormat(&raw_format);
    const CoTaskMemPtr<WAVEFORMATEX> format(raw_format);
    if (FAILED(hr)) {
        log_hresult(logger, common::LogLevel::kWarning, "get mix format", hr);
        return common::make_unexpected(to_device_error(hr));
    }
    return summarize_format(*format);
}

}  // namespace vocroz::device::win
