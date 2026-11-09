// SPDX-License-Identifier: AGPL-3.0-only
//
// 出力デバイスの抜去の通知を受け取るリスナー。
// 通知を受けたスレッドでそのまま Mixer の安全ミュートをかけ（音を止めるのを待たせない）、
// session への通知とデバイスの後始末はコントロールスレッドへ移して行う
// （docs/adr/0023-wasapi-device-io.md、docs/adr/0028-engine-assembly.md）。
#pragma once

#include <functional>

#include "vocroz/audio_core/mixer.h"
#include "vocroz/audio_core/ports/audio_device_listener.h"

#include "control_loop.h"

namespace vocroz::engine {

/// 出力デバイスの抜去の受け口。
/// スレッド安全性: on_device_lost() はデバイスのアダプターの任意のスレッド（オーディオスレッドを
/// 除く）から呼ばれる。on_lost はコントロールスレッドで呼ぶ。
class DeviceLossRelay final : public audio_core::AudioDeviceListener {
public:
    /// @param mixer   安全ミュートをかけるミキサー。このオブジェクトより長く生存させる
    /// @param loop    後始末を積むループ。このオブジェクトより長く生存させる
    /// @param on_lost コントロールスレッドで行う後始末（session への通知とデバイスの停止）
    DeviceLossRelay(audio_core::Mixer& mixer, ControlLoop& loop, std::function<void()> on_lost)
        : mixer_(mixer), loop_(loop), on_lost_(std::move(on_lost)) {}

    void on_device_lost() noexcept override;

private:
    audio_core::Mixer& mixer_;
    ControlLoop& loop_;
    std::function<void()> on_lost_;
};

}  // namespace vocroz::engine
