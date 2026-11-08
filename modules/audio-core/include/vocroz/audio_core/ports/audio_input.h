// SPDX-License-Identifier: AGPL-3.0-only
//
// 物理デバイスからの音声入力のポート。device と capture が WASAPI で実装し、
// テストでは FakeAudioInput を使う。
#pragma once

#include <cstddef>

#include "vocroz/audio_core/audio_device_error.h"
#include "vocroz/audio_core/ports/audio_capture_callback.h"
#include "vocroz/audio_core/ports/audio_device_listener.h"
#include "vocroz/common/result.h"

namespace vocroz::audio_core {

/// 入力デバイス。1 つのインスタンスが 1 つのデバイスを表す。
/// 実装は内部フォーマット（kSampleRate、Float32）でコールバックを呼び、
/// デバイスのサンプルレートが違う場合は実装側で変換する。
class AudioInput {
public:
    virtual ~AudioInput() = default;

    /// キャプチャを開始し、以後デバイスの周期ごとにオーディオスレッドで callback.on_captured()
    /// を呼ぶ。
    /// @param callback キャプチャしたサンプルを受け取るリスナー。stop() が戻るまで生存させる
    /// @param listener デバイスの抜去を受け取るリスナー。stop() が戻るまで生存させる
    /// @return 開始できたら成功。失敗の原因は AudioDeviceError
    /// エラー時の振る舞い: 失敗したら callback を呼ばない。別のデバイスへフォールバックしない。
    /// スレッド安全性: コントロールスレッドから呼ぶ。
    [[nodiscard]] virtual common::Result<void, AudioDeviceError> start(
        AudioCaptureCallback& callback, AudioDeviceListener& listener) = 0;

    /// キャプチャを停止する。戻った時点で callback と listener
    /// はもう呼ばれない。開始前に呼んでもよい。 スレッド安全性: コントロールスレッドから呼ぶ。
    virtual void stop() noexcept = 0;

    /// コールバックに渡すバッファの 1 フレームあたりのチャンネル数を返す。
    /// スレッド安全性: どのスレッドからも呼び出し可能。
    [[nodiscard]] virtual std::size_t channel_count() const noexcept = 0;
};

}  // namespace vocroz::audio_core
