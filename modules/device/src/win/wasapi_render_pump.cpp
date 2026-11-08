// SPDX-License-Identifier: AGPL-3.0-only
//
// wasapi_render_pump.h の実装。

#include "win/wasapi_render_pump.h"

#include <span>

namespace vocroz::device::win {

HRESULT WasapiRenderPump::prepare(IAudioClient& client, UINT32 buffer_frames) {
    if (callback_ == nullptr) {
        return E_UNEXPECTED;  // コールバックを設定せずに開始した（プログラムの誤り）
    }
    buffer_frames_ = buffer_frames;
    return client.GetService(IID_PPV_ARGS(render_client_.ReleaseAndGetAddressOf()));
}

HRESULT WasapiRenderPump::pump(IAudioClient& client) noexcept VOCROZ_NONBLOCKING {
    UINT32 padding = 0;
    HRESULT hr = client.GetCurrentPadding(&padding);
    if (FAILED(hr)) {
        return hr;
    }
    // まだ再生されていないぶん（padding）を除いた空きだけを書く。空きをすべて埋めるのは、
    // イベントの遅れで次の周期に間に合わなかったぶんも、同じ呼び出しで取り戻すため。
    const UINT32 frames = buffer_frames_ - padding;
    if (frames == 0) {
        return S_OK;
    }
    BYTE* data = nullptr;
    hr = render_client_->GetBuffer(frames, &data);
    if (FAILED(hr)) {
        return hr;
    }
    // ストリームは 32 ビット浮動小数点のインターリーブ形式で開いている（stream_plan）。
    const std::span<float> samples(reinterpret_cast<float*>(data), frames * channels_);
    callback_->render(audio_core::InterleavedBuffer{samples, channels_});
    return render_client_->ReleaseBuffer(frames, 0);
}

void WasapiRenderPump::release() noexcept {
    render_client_.Reset();
}

}  // namespace vocroz::device::win
