// SPDX-License-Identifier: AGPL-3.0-only
//
// session の OutputDeviceControl を、audio-core の AudioDeviceCatalog と AudioOutput で実装する。
// 開いた出力にはミキサーを描画のコールバックとして渡し、抜去は DeviceLossRelay に知らせる。
// デバイスを開く処理は差し替えられるようにし、Linux でもフェイクで単体テストする。
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "vocroz/audio_core/audio_device_error.h"
#include "vocroz/audio_core/ports/audio_device_catalog.h"
#include "vocroz/audio_core/ports/audio_device_listener.h"
#include "vocroz/audio_core/ports/audio_output.h"
#include "vocroz/audio_core/ports/audio_render_callback.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"
#include "vocroz/session/ports/output_device_control.h"

namespace vocroz::engine {

/// 親機の出力デバイスの一覧、開く、閉じる。開いている出力は同時に 1 つだけ。
/// スレッド安全性: コントロールスレッドから使う。
class EngineOutputDevices final : public session::OutputDeviceControl {
public:
    /// エンドポイント ID の出力デバイスを作る処理（device の make_wasapi_audio_output()）。
    using OutputFactory =
        std::function<common::Result<std::unique_ptr<audio_core::AudioOutput>,
                                     audio_core::AudioDeviceError>(std::string_view endpoint_id)>;

    /// 引数はすべて、このオブジェクトより長く生存させる。
    /// @param catalog  出力デバイスの一覧
    /// @param factory  出力デバイスを作る処理
    /// @param render   開いた出力が呼ぶ描画（Mixer）
    /// @param listener 開いた出力の抜去の受け口（DeviceLossRelay）
    /// @param logger   失敗の記録先
    EngineOutputDevices(audio_core::AudioDeviceCatalog& catalog, OutputFactory factory,
                        audio_core::AudioRenderCallback& render,
                        audio_core::AudioDeviceListener& listener, common::Logger& logger);
    ~EngineOutputDevices() override;

    EngineOutputDevices(const EngineOutputDevices&) = delete;
    EngineOutputDevices& operator=(const EngineOutputDevices&) = delete;

    std::vector<control_protocol::OutputDevice> list_outputs() override;
    common::Result<std::string, control_protocol::ErrorCode> open_output(
        const std::string& device_id) override;
    void close_output() override;

private:
    audio_core::AudioDeviceCatalog& catalog_;
    OutputFactory factory_;
    audio_core::AudioRenderCallback& render_;
    audio_core::AudioDeviceListener& listener_;
    common::Logger& logger_;
    std::unique_ptr<audio_core::AudioOutput> output_;
};

}  // namespace vocroz::engine
