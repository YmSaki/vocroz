// SPDX-License-Identifier: AGPL-3.0-only
//
// 入力のストリームで、デバイスが用意したパケットを AudioCaptureCallback に渡す。
#pragma once

#include <cstddef>
#include <vector>

#include <audioclient.h>
#include <windows.h>
#include <wrl/client.h>

#include "vocroz/audio_core/ports/audio_capture_callback.h"

#include "win/wasapi_stream_pump.h"

namespace vocroz::device::win {

/// 入力のバッファの受け渡し。
class WasapiCapturePump final : public WasapiStreamPump {
public:
    /// @param channels バッファのチャンネル数
    explicit WasapiCapturePump(std::size_t channels) noexcept : channels_(channels) {}

    /// キャプチャしたサンプルを受け取るコールバックを設定する。
    /// スレッド安全性: ストリームを開始する前に、コントロールスレッドから呼ぶ。
    void set_callback(audio_core::AudioCaptureCallback& callback) noexcept {
        callback_ = &callback;
    }

    [[nodiscard]] HRESULT prepare(IAudioClient& client, UINT32 buffer_frames) override;
    [[nodiscard]] HRESULT pump(IAudioClient& client) noexcept VOCROZ_NONBLOCKING override;
    void release() noexcept override;

private:
    // 無音のパケットのぶんだけ、無音のサンプルを渡す。
    void deliver_silence(UINT32 frames) noexcept VOCROZ_NONBLOCKING;

    const std::size_t channels_;
    audio_core::AudioCaptureCallback* callback_ = nullptr;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> capture_client_;
    // AUDCLNT_BUFFERFLAGS_SILENT のパケットはデータを読んではいけないので、代わりに渡す無音。
    // オーディオスレッドで確保しないよう、prepare() でバッファ 1 つぶんを確保しておく。
    std::vector<float> silence_;
};

}  // namespace vocroz::device::win
