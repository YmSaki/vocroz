// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の MidiInput。テストが receive() に渡したメッセージを、同じスレッドでコールバックに渡す。
#pragma once

#include "vocroz/audio_core/ports/midi_input.h"

namespace vocroz::audio_core {

/// テストが駆動する MIDI 入力ポート。
/// スレッド安全性: 1 つのスレッドから使う。
class FakeMidiInput final : public MidiInput {
public:
    [[nodiscard]] common::Result<void, AudioDeviceError> start(
        MidiInputCallback& callback, AudioDeviceListener& listener) override {
        if (lost_) {
            return common::make_unexpected(AudioDeviceError::kDeviceNotFound);
        }
        if (callback_ != nullptr) {
            return common::make_unexpected(AudioDeviceError::kAlreadyStarted);
        }
        callback_ = &callback;
        listener_ = &listener;
        return {};
    }

    void stop() noexcept override {
        callback_ = nullptr;
        listener_ = nullptr;
    }

    /// 受け取ったメッセージとしてコールバックに渡す。開始していなければ何もしない。
    void receive(const TimestampedMidiMessage& message) {
        if (callback_ != nullptr) {
            callback_->on_midi_message(message);
        }
    }

    /// ポートの抜去を模擬する。リスナーに通知し、以後コールバックを呼ばない。
    void simulate_device_lost() {
        lost_ = true;
        AudioDeviceListener* listener = listener_;
        stop();
        if (listener != nullptr) {
            listener->on_device_lost();
        }
    }

    /// 開始しているなら true。
    [[nodiscard]] bool started() const noexcept {
        return callback_ != nullptr;
    }

private:
    MidiInputCallback* callback_ = nullptr;
    AudioDeviceListener* listener_ = nullptr;
    bool lost_ = false;
};

}  // namespace vocroz::audio_core
