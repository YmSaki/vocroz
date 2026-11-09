// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の MidiDeviceCatalog。テストが設定した一覧を返す。
#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "vocroz/audio_core/ports/midi_device_catalog.h"

namespace vocroz::audio_core {

/// テストが一覧を決める MIDI デバイスの一覧。
/// スレッド安全性: 1 つのスレッドから使う。
class FakeMidiDeviceCatalog final : public MidiDeviceCatalog {
public:
    [[nodiscard]] common::Result<std::vector<MidiDeviceInfo>, AudioDeviceError> list() override {
        if (error_) {
            return common::make_unexpected(*error_);
        }
        return devices_;
    }

    /// list() が返す一覧を設定する。
    void set_devices(std::vector<MidiDeviceInfo> devices) {
        devices_ = std::move(devices);
    }

    /// list() を失敗させる。std::nullopt で失敗をやめる。
    void set_error(std::optional<AudioDeviceError> error) {
        error_ = error;
    }

private:
    std::vector<MidiDeviceInfo> devices_;
    std::optional<AudioDeviceError> error_;
};

}  // namespace vocroz::audio_core
