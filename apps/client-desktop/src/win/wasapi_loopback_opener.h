// SPDX-License-Identifier: AGPL-3.0-only
//
// アプリ音声の InputDeviceOpener の WASAPI の実装。capture の make_wasapi_loopback_input() で開く。
// 子機のキャプチャ（ClientCapture）は、アプリ音声を加えたときと、既定の再生デバイスが切り替わって
// ループバックが止まったときに開き直すので、コンポジションルートがこれを注入する。
// WASAPI は Windows にしかないので、Windows のビルドだけに入れる。
#pragma once

#include <memory>
#include <string_view>

#include "vocroz/capture/factories.h"
#include "vocroz/common/ports/logger.h"

#include "input_device_opener.h"

namespace vocroz::client::win {

/// 既定の再生デバイスのループバックを開く InputDeviceOpener。
/// スレッド安全性: コントロールスレッドから呼ぶ。
class WasapiLoopbackOpener final : public InputDeviceOpener {
public:
    /// @param logger デバイスのエラーの出力先。このオブジェクトより長く生存させる
    explicit WasapiLoopbackOpener(common::Logger& logger) noexcept : logger_(logger) {}

    /// 既定の再生デバイスを集める入力を返す。device_id は使わない（ClientCapture
    /// は空文字列を渡す）。
    [[nodiscard]] common::Result<std::unique_ptr<audio_core::AudioInput>,
                                 audio_core::AudioDeviceError>
    open(std::string_view /*device_id*/) override {
        return capture::make_wasapi_loopback_input(logger_);
    }

private:
    common::Logger& logger_;
};

}  // namespace vocroz::client::win
