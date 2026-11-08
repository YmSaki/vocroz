// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の AudioOutput。デバイスのコールバックの代わりに、テストが render_frames()
// を呼んだときだけ
// 同じスレッドでコールバックを呼ぶ。コールバック長と抜去のタイミングをテストが決められる。
#pragma once

#include <cstddef>
#include <vector>

#include "vocroz/audio_core/ports/audio_output.h"

namespace vocroz::audio_core {

/// テストが駆動する出力デバイス。
/// スレッド安全性: 1 つのスレッドから使う。
class FakeAudioOutput final : public AudioOutput {
public:
    /// @param channel_count 出力のチャンネル数
    explicit FakeAudioOutput(std::size_t channel_count = 2) : channel_count_(channel_count) {}

    [[nodiscard]] common::Result<void, AudioDeviceError> start(
        AudioRenderCallback& callback, AudioDeviceListener& listener) override {
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

    /// 1 回のコールバックを呼び、書き込まれたサンプルを返す。開始していなければ空を返す。
    /// @param frames コールバック長（フレーム数）
    /// @return インターリーブ形式のサンプル（frames × channel_count()）
    std::vector<float> render_frames(std::size_t frames) {
        if (callback_ == nullptr) {
            return {};
        }
        // コールバックが書き込まなかったサンプルを見分けられるよう、0 以外の値で埋めておく。
        std::vector<float> buffer(frames * channel_count_, kUnwrittenSample);
        callback_->render(InterleavedBuffer{buffer, channel_count_});
        return buffer;
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

    /// 開始していれば true を返す。
    [[nodiscard]] bool is_started() const noexcept {
        return callback_ != nullptr;
    }

    /// コールバックが書き込まなかったサンプルの値。
    static constexpr float kUnwrittenSample = 12345.0F;

private:
    std::size_t channel_count_;
    AudioRenderCallback* callback_ = nullptr;
    AudioDeviceListener* listener_ = nullptr;
    bool lost_ = false;
};

}  // namespace vocroz::audio_core
