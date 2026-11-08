// SPDX-License-Identifier: AGPL-3.0-only
//
// WASAPI による AudioInput の実装。生成は factories.h の make_wasapi_audio_input() から行う。
#pragma once

#include <cstddef>
#include <string>

#include "vocroz/audio_core/ports/audio_input.h"
#include "vocroz/common/ports/logger.h"

#include "win/wasapi_capture_pump.h"
#include "win/wasapi_stream.h"

namespace vocroz::device::win {

/// エンドポイント ID で指定した 1 台の入力デバイス。子機のマイク入力と親機のローカル入力に使う。
class WasapiAudioInput final : public audio_core::AudioInput {
public:
    /// @param endpoint_id 入力デバイスのエンドポイント ID（UTF-8）
    /// @param channels    デバイスのミックスフォーマットのチャンネル数
    /// @param logger      開始の失敗と抜去の詳細の出力先
    WasapiAudioInput(std::string endpoint_id, std::size_t channels, common::Logger& logger);

    [[nodiscard]] common::Result<void, audio_core::AudioDeviceError> start(
        audio_core::AudioCaptureCallback& callback,
        audio_core::AudioDeviceListener& listener) override;
    void stop() noexcept override;
    [[nodiscard]] std::size_t channel_count() const noexcept override;

private:
    WasapiCapturePump pump_;
    // pump_ より後に宣言し、先に破棄する（ストリームのスレッドが pump_ を使い終えてから破棄する）。
    WasapiStream stream_;
};

}  // namespace vocroz::device::win
