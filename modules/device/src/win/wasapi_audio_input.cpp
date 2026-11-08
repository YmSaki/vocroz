// SPDX-License-Identifier: AGPL-3.0-only
//
// wasapi_audio_input.h の実装。

#include "win/wasapi_audio_input.h"

#include <utility>

namespace vocroz::device::win {

WasapiAudioInput::WasapiAudioInput(std::string endpoint_id, std::size_t channels,
                                   common::Logger& logger)
    : pump_(channels), stream_(std::move(endpoint_id), eCapture, channels, logger) {}

common::Result<void, audio_core::AudioDeviceError> WasapiAudioInput::start(
    audio_core::AudioCaptureCallback& callback, audio_core::AudioDeviceListener& listener) {
    // 動いているストリームのコールバックを差し替えないよう、先に二重の開始を断る。
    if (stream_.is_active()) {
        return common::make_unexpected(audio_core::AudioDeviceError::kAlreadyStarted);
    }
    pump_.set_callback(callback);
    return stream_.start(pump_, listener);
}

void WasapiAudioInput::stop() noexcept {
    stream_.stop();
}

std::size_t WasapiAudioInput::channel_count() const noexcept {
    return stream_.channel_count();
}

}  // namespace vocroz::device::win
