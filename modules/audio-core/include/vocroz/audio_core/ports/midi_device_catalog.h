// SPDX-License-Identifier: AGPL-3.0-only
//
// 使用できる MIDI 入力ポートの一覧を取得するポート。device が JUCE の MidiInput で実装し、
// テストでは FakeMidiDeviceCatalog を使う（docs/adr/0029-midi-and-vsti.md）。
// 一覧は MIDI 入力の選択画面と、保存した ID がまだあるかの確認に使う。
#pragma once

#include <vector>

#include "vocroz/audio_core/audio_device_error.h"
#include "vocroz/audio_core/midi_device_info.h"
#include "vocroz/common/result.h"

namespace vocroz::audio_core {

/// MIDI 入力ポートの一覧。
class MidiDeviceCatalog {
public:
    virtual ~MidiDeviceCatalog() = default;

    /// 現在使用できる MIDI 入力ポートを列挙する。
    /// @return ポートの一覧（0 個なら空）。失敗の原因は AudioDeviceError
    /// エラー時の振る舞い: 一覧を取得できなければ kInternal を返す。
    /// スレッド安全性: コントロールスレッドから呼ぶ。オーディオスレッドからは呼ばない。
    [[nodiscard]] virtual common::Result<std::vector<MidiDeviceInfo>, AudioDeviceError> list() = 0;
};

}  // namespace vocroz::audio_core
