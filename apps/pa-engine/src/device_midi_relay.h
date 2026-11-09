// SPDX-License-Identifier: AGPL-3.0-only
//
// MIDI デバイスのキューの開閉の通知（JUCE のメッセージスレッド）を、コントロールスレッドへ移して
// ミキサーのグラフに反映する（docs/adr/0039-pa-engine-juce-message-thread.md）。
// EngineMixer はコントロールスレッドからだけ使う約束なので、メッセージスレッドからは直接呼ばない。
#pragma once

#include <memory>
#include <string>

#include "control_loop.h"
#include "device_midi_inputs.h"
#include "engine_mixer.h"

namespace vocroz::engine {

/// MIDI デバイスの開閉をグラフの MIDI ソースに反映する。
/// スレッド安全性: DeviceMidiListener の関数はメッセージスレッドから呼ばれ、ミキサーの操作は
/// コントロールスレッドで行う。
class DeviceMidiRelay final : public DeviceMidiListener {
public:
    /// @param loop  コントロールスレッドのループ。このオブジェクトより長く生存させる
    /// @param mixer MIDI ソースをつなぐミキサー。このオブジェクトより長く生存させる
    DeviceMidiRelay(ControlLoop& loop, EngineMixer& mixer) : loop_(loop), mixer_(mixer) {}

    void on_midi_device_opened(const std::string& device_id,
                               std::shared_ptr<audio_core::MidiInputQueue> queue) override;
    void on_midi_device_closed(const std::string& device_id) override;

private:
    ControlLoop& loop_;
    EngineMixer& mixer_;
};

}  // namespace vocroz::engine
