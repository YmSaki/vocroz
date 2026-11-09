// SPDX-License-Identifier: AGPL-3.0-only
//
// InputDeviceOpener の WASAPI の実装。device の make_wasapi_audio_input() で開く。
// 子機のキャプチャ（ClientCapture）は入力を選び直すたびに開き直すので、コンポジションルートが
// これを注入する。WASAPI は Windows にしかないので、Windows のビルドだけに入れる。
#pragma once

#include <memory>
#include <string_view>

#include "vocroz/common/ports/logger.h"
#include "vocroz/device/factories.h"

#include "input_device_opener.h"

namespace vocroz::client::win {

/// WASAPI で入力デバイスを開く InputDeviceOpener。
/// スレッド安全性: コントロールスレッドから呼ぶ。
class WasapiInputDeviceOpener final : public InputDeviceOpener {
public:
    /// @param logger デバイスのエラーの出力先。このオブジェクトより長く生存させる
    explicit WasapiInputDeviceOpener(common::Logger& logger) noexcept : logger_(logger) {}

    [[nodiscard]] common::Result<std::unique_ptr<audio_core::AudioInput>,
                                 audio_core::AudioDeviceError>
    open(std::string_view device_id) override {
        return device::make_wasapi_audio_input(device_id, logger_);
    }

private:
    common::Logger& logger_;
};

}  // namespace vocroz::client::win
