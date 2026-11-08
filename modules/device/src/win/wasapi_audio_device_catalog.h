// SPDX-License-Identifier: AGPL-3.0-only
//
// WASAPI（MMDevice API）による AudioDeviceCatalog の実装。
// 生成は factories.h の make_wasapi_audio_device_catalog() から行う。
#pragma once

#include <vector>

#include "vocroz/audio_core/ports/audio_device_catalog.h"
#include "vocroz/common/ports/logger.h"

namespace vocroz::device::win {

/// 有効なオーディオエンドポイントの一覧。
class WasapiAudioDeviceCatalog final : public audio_core::AudioDeviceCatalog {
public:
    /// @param logger 一覧の取得に失敗したときの詳細の出力先
    explicit WasapiAudioDeviceCatalog(common::Logger& logger) noexcept : logger_(logger) {}

    [[nodiscard]] common::Result<std::vector<audio_core::AudioDeviceInfo>,
                                 audio_core::AudioDeviceError>
    list(audio_core::AudioDeviceDirection direction) override;

private:
    common::Logger& logger_;
};

}  // namespace vocroz::device::win
