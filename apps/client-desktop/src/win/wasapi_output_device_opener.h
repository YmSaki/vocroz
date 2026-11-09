// SPDX-License-Identifier: AGPL-3.0-only
//
// OutputDeviceOpener の WASAPI の実装。device の make_wasapi_audio_output() で開く。
// 返送モニターの出力は出演者が選び直すたびに開き直すので、コンポジションルートがこれを注入する。
// WASAPI は Windows にしかないので、Windows のビルドだけに入れる。
#pragma once

#include <memory>
#include <string_view>

#include "vocroz/common/ports/logger.h"
#include "vocroz/device/factories.h"

#include "output_device_opener.h"

namespace vocroz::client::win {

/// WASAPI で出力デバイスを開く OutputDeviceOpener。
/// スレッド安全性: コントロールスレッドから呼ぶ。
class WasapiOutputDeviceOpener final : public OutputDeviceOpener {
public:
    /// @param logger デバイスのエラーの出力先。このオブジェクトより長く生存させる
    explicit WasapiOutputDeviceOpener(common::Logger& logger) noexcept : logger_(logger) {}

    [[nodiscard]] common::Result<std::unique_ptr<audio_core::AudioOutput>,
                                 audio_core::AudioDeviceError>
    open(std::string_view device_id) override {
        return device::make_wasapi_audio_output(device_id, logger_);
    }

private:
    common::Logger& logger_;
};

}  // namespace vocroz::client::win
