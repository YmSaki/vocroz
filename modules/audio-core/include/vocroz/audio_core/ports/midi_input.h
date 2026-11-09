// SPDX-License-Identifier: AGPL-3.0-only
//
// MIDI 入力ポートのポート。device が JUCE の MidiInput で実装し、テストでは FakeMidiInput を使う
// （docs/adr/0009-beta-module-layout.md、docs/adr/0029-midi-and-vsti.md）。
// 抜去は AudioDeviceListener で通知するだけで、Panic の判断は受け取った側（session）が行う。
#pragma once

#include "vocroz/audio_core/audio_device_error.h"
#include "vocroz/audio_core/ports/audio_device_listener.h"
#include "vocroz/audio_core/ports/midi_input_callback.h"
#include "vocroz/common/result.h"

namespace vocroz::audio_core {

/// MIDI 入力ポート。1 つのインスタンスが 1 つのポートを表す。
class MidiInput {
public:
    virtual ~MidiInput() = default;

    /// 受信を開始し、以後メッセージが届くたびに callback.on_midi_message() を呼ぶ。
    /// @param callback メッセージを受け取るコールバック。stop() が戻るまで生存させる
    /// @param listener ポートの抜去を受け取るリスナー。stop() が戻るまで生存させる
    /// @return 開始できたら成功。失敗の原因は AudioDeviceError
    /// エラー時の振る舞い: 失敗したら callback を呼ばない。別のポートへフォールバックしない。
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    [[nodiscard]] virtual common::Result<void, AudioDeviceError> start(
        MidiInputCallback& callback, AudioDeviceListener& listener) = 0;

    /// 受信を停止する。戻った時点で callback と listener はもう呼ばれない。開始前に呼んでもよい。
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    virtual void stop() noexcept = 0;
};

}  // namespace vocroz::audio_core
