// SPDX-License-Identifier: AGPL-3.0-only
//
// factories.h の実装。

#include "vocroz/capture/factories.h"

#include "win/wasapi_loopback_input.h"

namespace vocroz::capture {

common::Result<std::unique_ptr<audio_core::AudioInput>, audio_core::AudioDeviceError>
make_wasapi_loopback_input(common::Logger& logger) {
    // 既定の再生デバイスは開始のたびに選び直すので、ここではデバイスに触れない。
    return std::unique_ptr<audio_core::AudioInput>(
        std::make_unique<win::WasapiLoopbackInput>(logger));
}

}  // namespace vocroz::capture
