// SPDX-License-Identifier: AGPL-3.0-only
//
// wasapi_capture_pump.h の実装。

#include "win/wasapi_capture_pump.h"

#include <algorithm>
#include <span>

namespace vocroz::device::win {

HRESULT WasapiCapturePump::prepare(IAudioClient& client, UINT32 buffer_frames) {
    if (callback_ == nullptr || buffer_frames == 0) {
        return E_UNEXPECTED;  // コールバックがないか、バッファ長が不正
    }
    silence_.assign(static_cast<std::size_t>(buffer_frames) * channels_, 0.0F);
    return client.GetService(IID_PPV_ARGS(capture_client_.ReleaseAndGetAddressOf()));
}

HRESULT WasapiCapturePump::pump(IAudioClient& /*client*/) noexcept VOCROZ_NONBLOCKING {
    // 1 回のイベントで複数のパケットがたまっていることがあるので、空になるまで読む。
    for (;;) {
        UINT32 packet_frames = 0;
        HRESULT hr = capture_client_->GetNextPacketSize(&packet_frames);
        if (FAILED(hr) || packet_frames == 0) {
            return hr;
        }
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        hr = capture_client_->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
        if (FAILED(hr)) {
            return hr;
        }
        if (hr == AUDCLNT_S_BUFFER_EMPTY) {
            return S_OK;
        }
        if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
            deliver_silence(frames);
        } else {
            // ストリームは 32 ビット浮動小数点のインターリーブ形式で開いている（stream_plan）。
            const std::span<const float> samples(reinterpret_cast<const float*>(data),
                                                 frames * channels_);
            callback_->on_captured(audio_core::ConstInterleavedBuffer{samples, channels_});
        }
        hr = capture_client_->ReleaseBuffer(frames);
        if (FAILED(hr)) {
            return hr;
        }
    }
}

void WasapiCapturePump::deliver_silence(UINT32 frames) noexcept VOCROZ_NONBLOCKING {
    const std::size_t capacity = silence_.size() / channels_;
    std::size_t remaining = frames;
    while (remaining > 0) {
        const std::size_t chunk = std::min(remaining, capacity);
        const std::span<const float> samples(silence_.data(), chunk * channels_);
        callback_->on_captured(audio_core::ConstInterleavedBuffer{samples, channels_});
        remaining -= chunk;
    }
}

void WasapiCapturePump::release() noexcept {
    capture_client_.Reset();
}

}  // namespace vocroz::device::win
