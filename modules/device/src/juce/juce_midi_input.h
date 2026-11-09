// SPDX-License-Identifier: AGPL-3.0-only
//
// JUCE の MidiInput による 1 つの MIDI 入力ポート（MidiInput の実装）。
// 受け取ったチャンネルメッセージを Clock の時刻に写してコールバックへ渡し、抜去を
// AudioDeviceListener に 1 回だけ知らせる。
#pragma once

#include <atomic>
#include <memory>
#include <string>

#include <juce_audio_devices/juce_audio_devices.h>

#include "vocroz/audio_core/ports/midi_input.h"
#include "vocroz/common/ports/clock.h"
#include "vocroz/common/ports/logger.h"

namespace vocroz::device {

/// JUCE による MIDI 入力ポート。
///
/// JUCE の MIDI のポートの一覧、開閉、抜去の通知は、JUCE のメッセージスレッドで動く。
/// そのため start() と stop() もメッセージスレッドから呼び、抜去は JUCE の
/// ump::DisconnectionListener でメッセージスレッドに届いたものをそのまま知らせる。
/// スレッド安全性: start() と stop() は JUCE のメッセージスレッドから呼ぶ。コールバックは JUCE の
/// MIDI の受信スレッドから、抜去の通知はメッセージスレッドから呼ばれる。
class JuceMidiInput final : public audio_core::MidiInput,
                            private juce::MidiInputCallback,
                            private juce::ump::DisconnectionListener {
public:
    /// @param identifier 開くポートの ID（MidiDeviceInfo::id）
    /// @param clock      タイムスタンプを写す先の時計。Mixer に注入するものと同じにする
    /// @param logger     開始の失敗と抜去の出力先
    JuceMidiInput(std::string identifier, const common::Clock& clock, common::Logger& logger);
    ~JuceMidiInput() override;

    JuceMidiInput(const JuceMidiInput&) = delete;
    JuceMidiInput& operator=(const JuceMidiInput&) = delete;

    [[nodiscard]] common::Result<void, audio_core::AudioDeviceError> start(
        audio_core::MidiInputCallback& callback,
        audio_core::AudioDeviceListener& listener) override;
    void stop() noexcept override;

private:
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;
    void disconnected() override;

    const std::string identifier_;
    const common::Clock& clock_;
    common::Logger& logger_;

    std::unique_ptr<juce::MidiInput> device_;
    // 受信スレッドが読むので atomic にする。stop() は device_ を破棄してから外す。
    std::atomic<audio_core::MidiInputCallback*> callback_{nullptr};
    audio_core::AudioDeviceListener* listener_ = nullptr;
    // 抜去を知らせた後は、届いたメッセージを渡さない（listener の約束）。
    std::atomic<bool> lost_{false};
};

}  // namespace vocroz::device
