// SPDX-License-Identifier: AGPL-3.0-only
//
// MIDI 入力ポートから届いたメッセージを受け取るコールバック。
// 親機は MidiInputQueue に積み、子機は MIDI レーンで親機へ送る。受け取った後の行き先は
// 使う側が決めるので、MidiInput は MidiInputQueue を直接は知らない。
#pragma once

#include "vocroz/audio_core/midi_input_queue.h"

namespace vocroz::audio_core {

/// MIDI 入力のメッセージを受け取るコールバック。
class MidiInputCallback {
public:
    virtual ~MidiInputCallback() = default;

    /// MIDI のチャンネルメッセージを 1 つ受け取る。届いた順に呼ばれる。
    /// SysEx、システムメッセージ、壊れたメッセージは渡さない。
    /// @param message 受け取ったメッセージ。timestamp は Mixer に注入する Clock と同じ基準の、
    ///                デバイスがメッセージを受け取った時刻
    /// スレッド安全性: MIDI 入力のアダプターの 1 つのスレッドから呼ばれる。実装はブロックしない。
    virtual void on_midi_message(const TimestampedMidiMessage& message) noexcept = 0;
};

}  // namespace vocroz::audio_core
