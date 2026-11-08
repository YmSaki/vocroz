// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の AudioDeviceCatalog。テストが設定したデバイスの一覧をそのまま返す。
#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "vocroz/audio_core/ports/audio_device_catalog.h"

namespace vocroz::audio_core {

/// テストが内容を決めるデバイスの一覧。
/// スレッド安全性: 1 つのスレッドから使う。
class FakeAudioDeviceCatalog final : public AudioDeviceCatalog {
public:
    [[nodiscard]] common::Result<std::vector<AudioDeviceInfo>, AudioDeviceError> list(
        AudioDeviceDirection direction) override {
        if (error_) {
            return common::make_unexpected(*error_);
        }
        return direction == AudioDeviceDirection::kOutput ? outputs_ : inputs_;
    }

    /// list() が返す一覧を設定する。
    /// @param direction 設定する向き
    /// @param devices   返すデバイスの一覧
    void set_devices(AudioDeviceDirection direction, std::vector<AudioDeviceInfo> devices) {
        (direction == AudioDeviceDirection::kOutput ? outputs_ : inputs_) = std::move(devices);
    }

    /// 以後の list() を error で失敗させる。std::nullopt で成功に戻す。
    void set_error(std::optional<AudioDeviceError> error) {
        error_ = error;
    }

private:
    std::vector<AudioDeviceInfo> outputs_;
    std::vector<AudioDeviceInfo> inputs_;
    std::optional<AudioDeviceError> error_;
};

}  // namespace vocroz::audio_core
