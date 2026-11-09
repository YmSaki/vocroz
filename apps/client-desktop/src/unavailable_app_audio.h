// SPDX-License-Identifier: AGPL-3.0-only
//
// アプリ音声を収集できないときの InputDeviceOpener。開こうとするとつねに kDeviceNotFound を返す。
// WASAPI のループバック（capture モジュール）をつなぐまでと、ループバックを使えない環境で使う。
// 子機はマイクだけを送り、アプリ音声の入力はキャプチャを始めない（音の経路を増やさない側に倒す）。
#pragma once

#include <memory>
#include <string_view>

#include "input_device_opener.h"

namespace vocroz::client {

/// つねに開けない InputDeviceOpener。
/// スレッド安全性: どのスレッドから呼んでもよい（状態を持たない）。
class UnavailableAppAudio final : public InputDeviceOpener {
public:
    /// kDeviceNotFound を返す。
    [[nodiscard]] common::Result<std::unique_ptr<audio_core::AudioInput>,
                                 audio_core::AudioDeviceError>
    open(std::string_view /*device_id*/) override {
        return common::make_unexpected(audio_core::AudioDeviceError::kDeviceNotFound);
    }
};

}  // namespace vocroz::client
