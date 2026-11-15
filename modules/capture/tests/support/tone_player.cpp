// SPDX-License-Identifier: AGPL-3.0-only
//
// tone_player.h の実装。

#include "support/tone_player.h"

#include <chrono>
#include <cmath>
#include <numbers>

#include <audioclient.h>
#include <combaseapi.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <windows.h>
#include <wrl/client.h>

namespace vocroz::capture::test_support {
namespace {

using Microsoft::WRL::ComPtr;

// start() が待つ、再生のスレッドの準備の状態。
constexpr int kPending = 0;
constexpr int kPlaying = 1;
constexpr int kUnavailable = 2;

// 鳴らす周波数（Hz）。可聴域の中ほどで、どの再生デバイスでもそのまま鳴る値にする。
constexpr double kToneHz = 1000.0;
// 再生ストリームのバッファ長（100ns 単位、100ms）。補充の間隔（10ms）より十分長くする。
constexpr REFERENCE_TIME kBufferDuration = 1'000'000;
// バッファを補充する間隔。
constexpr auto kRefillInterval = std::chrono::milliseconds(10);

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT（ksmedia.h の GUID の実体をリンクしないため値を書く）。
constexpr GUID kSubtypeIeeeFloat = {
    0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

bool is_float32(const WAVEFORMATEX& format) noexcept {
    if (format.wBitsPerSample != 32) {
        return false;
    }
    if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return true;
    }
    return format.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
           reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format).SubFormat == kSubtypeIeeeFloat;
}

}  // namespace

TonePlayer::~TonePlayer() {
    stop();
}

bool TonePlayer::start() {
    stopping_.store(false, std::memory_order_relaxed);
    std::atomic<int> result{kPending};
    thread_ = std::thread([this, &result] { run(result); });
    while (result.load(std::memory_order_acquire) == kPending) {
        std::this_thread::yield();
    }
    if (result.load(std::memory_order_acquire) != kPlaying) {
        thread_.join();
        return false;
    }
    return true;
}

void TonePlayer::stop() noexcept {
    stopping_.store(true, std::memory_order_relaxed);
    if (thread_.joinable()) {
        thread_.join();
    }
}

void TonePlayer::run(std::atomic<int>& result) noexcept {
    if (FAILED(::CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        result.store(kUnavailable, std::memory_order_release);
        return;
    }
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioClient> client;
        ComPtr<IAudioRenderClient> render;
        WAVEFORMATEX* format = nullptr;
        UINT32 buffer_frames = 0;
        HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                        IID_PPV_ARGS(&enumerator));
        if (SUCCEEDED(hr)) {
            hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        }
        if (SUCCEEDED(hr)) {
            hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(client.GetAddressOf()));
        }
        if (SUCCEEDED(hr)) {
            hr = client->GetMixFormat(&format);
        }
        if (SUCCEEDED(hr) && !is_float32(*format)) {
            hr = E_NOTIMPL;
        }
        if (SUCCEEDED(hr)) {
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, kBufferDuration, 0, format,
                                    nullptr);
        }
        if (SUCCEEDED(hr)) {
            hr = client->GetBufferSize(&buffer_frames);
        }
        if (SUCCEEDED(hr)) {
            hr = client->GetService(IID_PPV_ARGS(&render));
        }
        if (SUCCEEDED(hr)) {
            hr = client->Start();
        }
        const UINT32 channels = format != nullptr ? format->nChannels : 0;
        const double step = format != nullptr ? 2.0 * std::numbers::pi * kToneHz /
                                                    static_cast<double>(format->nSamplesPerSec)
                                              : 0.0;
        ::CoTaskMemFree(format);
        if (FAILED(hr)) {
            result.store(kUnavailable, std::memory_order_release);
        } else {
            result.store(kPlaying, std::memory_order_release);
            double phase = 0.0;
            while (!stopping_.load(std::memory_order_relaxed)) {
                UINT32 padding = 0;
                BYTE* data = nullptr;
                if (SUCCEEDED(client->GetCurrentPadding(&padding)) && padding < buffer_frames &&
                    SUCCEEDED(render->GetBuffer(buffer_frames - padding, &data))) {
                    const UINT32 frames = buffer_frames - padding;
                    float* samples = reinterpret_cast<float*>(data);
                    for (UINT32 frame = 0; frame < frames; ++frame) {
                        const auto value = static_cast<float>(kAmplitude * std::sin(phase));
                        phase = std::fmod(phase + step, 2.0 * std::numbers::pi);
                        for (UINT32 channel = 0; channel < channels; ++channel) {
                            samples[frame * channels + channel] = value;
                        }
                    }
                    render->ReleaseBuffer(frames, 0);
                }
                std::this_thread::sleep_for(kRefillInterval);
            }
            client->Stop();
        }
    }
    ::CoUninitialize();
}

}  // namespace vocroz::capture::test_support
