// SPDX-License-Identifier: AGPL-3.0-only
//
// wasapi_audio_output.h の実装。

#include "win/wasapi_audio_output.h"

#include <utility>

namespace vocroz::device::win {

WasapiAudioOutput::WasapiAudioOutput(std::string endpoint_id, std::size_t channels,
                                     common::Logger& logger)
    : pump_(channels), stream_(std::move(endpoint_id), eRender, channels, logger) {}

common::Result<void, audio_core::AudioDeviceError> WasapiAudioOutput::start(
    audio_core::AudioRenderCallback& callback, audio_core::AudioDeviceListener& listener) {
    // 動いているストリームのコールバックを差し替えないよう、先に二重の開始を断る。
    if (stream_.is_active()) {
        return common::make_unexpected(audio_core::AudioDeviceError::kAlreadyStarted);
    }
    pump_.set_callback(callback);
    return stream_.start(pump_, listener);
}

void WasapiAudioOutput::stop() noexcept {
    stream_.stop();
}

std::size_t WasapiAudioOutput::channel_count() const noexcept {
    return stream_.channel_count();
}

}  // namespace vocroz::device::win
