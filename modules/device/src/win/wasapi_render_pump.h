// SPDX-License-Identifier: AGPL-3.0-only
//
// 出力のストリームで、デバイスのバッファの空きを AudioRenderCallback に書かせる。
#pragma once

#include <cstddef>

#include <audioclient.h>
#include <windows.h>
#include <wrl/client.h>

#include "vocroz/audio_core/ports/audio_render_callback.h"

#include "win/wasapi_stream_pump.h"

namespace vocroz::device::win {

/// 出力のバッファの受け渡し。
class WasapiRenderPump final : public WasapiStreamPump {
public:
    /// @param channels バッファのチャンネル数
    explicit WasapiRenderPump(std::size_t channels) noexcept : channels_(channels) {}

    /// サンプルを生成するコールバックを設定する。
    /// スレッド安全性: ストリームを開始する前に、コントロールスレッドから呼ぶ。
    void set_callback(audio_core::AudioRenderCallback& callback) noexcept {
        callback_ = &callback;
    }

    [[nodiscard]] HRESULT prepare(IAudioClient& client, UINT32 buffer_frames) override;
    [[nodiscard]] HRESULT pump(IAudioClient& client) noexcept VOCROZ_NONBLOCKING override;
    void release() noexcept override;

private:
    const std::size_t channels_;
    audio_core::AudioRenderCallback* callback_ = nullptr;
    Microsoft::WRL::ComPtr<IAudioRenderClient> render_client_;
    UINT32 buffer_frames_ = 0;
};

}  // namespace vocroz::device::win
