// SPDX-License-Identifier: AGPL-3.0-only
//
// 入力デバイスを ID で開くポート。コンポジションルートが device の
// make_wasapi_audio_input() で実装し、テストでは FakeAudioInput を返す実装に差し替える。
// 子機のキャプチャ（ClientCapture）が、選び直すたびに開き直すので、生成をここに切り出す。
#pragma once

#include <memory>
#include <string_view>

#include "vocroz/audio_core/audio_device_error.h"
#include "vocroz/audio_core/ports/audio_input.h"
#include "vocroz/common/result.h"

namespace vocroz::client {

/// 入力デバイスを開く。
class InputDeviceOpener {
public:
    virtual ~InputDeviceOpener() = default;

    /// ID のデバイスを開く。開くだけで、キャプチャは始めない。
    /// @param device_id デバイスの永続 ID
    /// @return 入力。ID のデバイスがなければ kDeviceNotFound。別のデバイスへは切り替えない
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    [[nodiscard]] virtual common::Result<std::unique_ptr<audio_core::AudioInput>,
                                         audio_core::AudioDeviceError>
    open(std::string_view device_id) = 0;
};

}  // namespace vocroz::client
