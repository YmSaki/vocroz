// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の AudioInput。テストが capture() に渡したサンプルを、同じスレッドでコールバックに渡す。
#pragma once

#include <cstddef>
#include <span>

#include "vocroz/audio_core/ports/audio_input.h"

namespace vocroz::audio_core {

/// テストが駆動する入力デバイス。
/// スレッド安全性: 1 つのスレッドから使う。
class FakeAudioInput final : public AudioInput {
public:
    /// @param channel_count 入力のチャンネル数
    explicit FakeAudioInput(std::size_t channel_count = 1) : channel_count_(channel_count) {}

    [[nodiscard]] common::Result<void, AudioDeviceError> start(
        AudioCaptureCallback& callback, AudioDeviceListener& listener) override {
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

    [[nodiscard]] std::size_t channel_count() const noexcept override {
        return channel_count_;
    }

    /// キャプチャしたサンプルとしてコールバックに渡す。開始していなければ何もしない。
    /// @param interleaved インターリーブ形式のサンプル（長さは channel_count() の倍数）
    void capture(std::span<const float> interleaved) {
        if (callback_ != nullptr) {
            callback_->on_captured(ConstInterleavedBuffer{interleaved, channel_count_});
        }
    }

    /// デバイスの抜去を模擬する。リスナーに通知し、以後コールバックを呼ばない。
    void simulate_device_lost() {
        lost_ = true;
        AudioDeviceListener* listener = listener_;
        stop();
        if (listener != nullptr) {
            listener->on_device_lost();
        }
    }

private:
    std::size_t channel_count_;
    AudioCaptureCallback* callback_ = nullptr;
    AudioDeviceListener* listener_ = nullptr;
    bool lost_ = false;
};

}  // namespace vocroz::audio_core
