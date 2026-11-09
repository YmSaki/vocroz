// SPDX-License-Identifier: AGPL-3.0-only
//
// JUCE の MidiInput::getAvailableDevices() による MIDI 入力ポートの一覧（MidiDeviceCatalog
// の実装）。
#pragma once

#include "vocroz/audio_core/ports/midi_device_catalog.h"

namespace vocroz::device {

/// JUCE による MIDI 入力ポートの一覧。ID は JUCE の MidiDeviceInfo::identifier
/// （Windows ではデバイスインターフェースのパスに基づく、抜き差しをまたいで変わらない値）。
/// スレッド安全性: JUCE のメッセージスレッドから呼ぶ。
class JuceMidiDeviceCatalog final : public audio_core::MidiDeviceCatalog {
public:
    [[nodiscard]] common::Result<std::vector<audio_core::MidiDeviceInfo>,
                                 audio_core::AudioDeviceError>
    list() override;
};

}  // namespace vocroz::device
