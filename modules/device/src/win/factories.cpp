// SPDX-License-Identifier: AGPL-3.0-only
//
// factories.h の実装。デバイスを開けるかを確かめてから、アダプターを生成する。

#include "vocroz/device/factories.h"

#include <string>

#include "detail/stream_plan.h"
#include "win/com_utils.h"
#include "win/wasapi_audio_device_catalog.h"
#include "win/wasapi_audio_input.h"
#include "win/wasapi_audio_output.h"
#include "win/wasapi_endpoint.h"

namespace vocroz::device {
namespace {

using audio_core::AudioDeviceError;

// エンドポイントが開ける向きと状態か確かめ、チャンネル数を返す。
// ポートの channel_count() は開始前から返せる必要があるので、生成の時点で決める。
common::Result<std::size_t, AudioDeviceError> probe_channels(std::string_view endpoint_id,
                                                             EDataFlow flow,
                                                             common::Logger& logger) {
    const win::ComApartment com;
    if (!com.is_ready()) {
        logger.log(common::LogLevel::kError, "device: COM is not available");
        return common::make_unexpected(AudioDeviceError::kInternal);
    }
    auto device = win::open_endpoint(endpoint_id, flow, logger);
    if (!device) {
        return common::make_unexpected(device.error());
    }
    const auto mix = win::read_mix_format(*device.value().Get(), logger);
    if (!mix) {
        return common::make_unexpected(mix.error());
    }
    // チャンネル数が扱える範囲かは、開き方の判断と同じ規則で確かめる。
    if (const auto plan = detail::plan_stream(mix.value(), std::nullopt); !plan) {
        return common::make_unexpected(plan.error());
    }
    return mix.value().channels;
}

}  // namespace

std::unique_ptr<audio_core::AudioDeviceCatalog> make_wasapi_audio_device_catalog(
    common::Logger& logger) {
    return std::make_unique<win::WasapiAudioDeviceCatalog>(logger);
}

common::Result<std::unique_ptr<audio_core::AudioOutput>, AudioDeviceError> make_wasapi_audio_output(
    std::string_view endpoint_id, common::Logger& logger) {
    const auto channels = probe_channels(endpoint_id, eRender, logger);
    if (!channels) {
        return common::make_unexpected(channels.error());
    }
    return std::unique_ptr<audio_core::AudioOutput>(std::make_unique<win::WasapiAudioOutput>(
        std::string(endpoint_id), channels.value(), logger));
}

common::Result<std::unique_ptr<audio_core::AudioInput>, AudioDeviceError> make_wasapi_audio_input(
    std::string_view endpoint_id, common::Logger& logger) {
    const auto channels = probe_channels(endpoint_id, eCapture, logger);
    if (!channels) {
        return common::make_unexpected(channels.error());
    }
    return std::unique_ptr<audio_core::AudioInput>(std::make_unique<win::WasapiAudioInput>(
        std::string(endpoint_id), channels.value(), logger));
}

}  // namespace vocroz::device
