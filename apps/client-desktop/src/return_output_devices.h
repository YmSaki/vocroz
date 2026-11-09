// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機の session の OutputDeviceControl を、audio-core の AudioDeviceCatalog と、出力デバイスを
// 開くポート（OutputDeviceOpener）で実装する。開いた出力には返送の再生（ReturnPlayer）を
// 描画のコールバックと抜去の受け口として渡す（docs/adr/0036-return-monitor.md）。
// 開けなければ別のデバイスへは切り替えず、何も開かないまま返す。
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "vocroz/audio_core/ports/audio_device_catalog.h"
#include "vocroz/audio_core/ports/audio_output.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/session/ports/output_device_control.h"

#include "output_device_opener.h"
#include "return_player.h"

namespace vocroz::client {

/// 返送モニターの出力デバイスの一覧、開く、閉じる。開いている出力は同時に 1 つだけ。
/// スレッド安全性: コントロールスレッドから使う。
class ReturnOutputDevices final : public session::OutputDeviceControl {
public:
    /// 引数はすべて、このオブジェクトより長く生存させる。
    /// @param catalog 出力デバイスの一覧
    /// @param opener  出力デバイスを開く
    /// @param player  開いた出力が呼ぶ返送の再生と、抜去の受け口
    /// @param logger  失敗の記録先
    ReturnOutputDevices(audio_core::AudioDeviceCatalog& catalog, OutputDeviceOpener& opener,
                        ReturnPlayer& player, common::Logger& logger) noexcept;
    ~ReturnOutputDevices() override;

    ReturnOutputDevices(const ReturnOutputDevices&) = delete;
    ReturnOutputDevices& operator=(const ReturnOutputDevices&) = delete;

    std::vector<control_protocol::OutputDevice> list_outputs() override;
    common::Result<std::string, control_protocol::ErrorCode> open_output(
        const std::string& device_id) override;
    void close_output() override;

private:
    audio_core::AudioDeviceCatalog& catalog_;
    OutputDeviceOpener& opener_;
    ReturnPlayer& player_;
    common::Logger& logger_;
    std::unique_ptr<audio_core::AudioOutput> output_;
};

}  // namespace vocroz::client
