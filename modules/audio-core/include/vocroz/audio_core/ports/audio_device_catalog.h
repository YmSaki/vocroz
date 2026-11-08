// SPDX-License-Identifier: AGPL-3.0-only
//
// 使用できるオーディオデバイスの一覧を取得するポート。device が WASAPI で実装し、
// テストでは FakeAudioDeviceCatalog を使う。
// 一覧は出力先と入力の選択画面と、保存したエンドポイント ID がまだあるかの確認に使う。
#pragma once

#include <vector>

#include "vocroz/audio_core/audio_device_error.h"
#include "vocroz/audio_core/audio_device_info.h"
#include "vocroz/common/result.h"

namespace vocroz::audio_core {

/// オーディオデバイスの一覧。
class AudioDeviceCatalog {
public:
    virtual ~AudioDeviceCatalog() = default;

    /// 現在使用できる（有効で、つながっている）デバイスを列挙する。
    /// @param direction 列挙するデバイスの向き
    /// @return デバイスの一覧（0 台なら空）。失敗の原因は AudioDeviceError
    /// エラー時の振る舞い: 一覧を取得できなければ kInternal を返す。
    /// スレッド安全性: コントロールスレッドから呼ぶ。オーディオスレッドからは呼ばない。
    [[nodiscard]] virtual common::Result<std::vector<AudioDeviceInfo>, AudioDeviceError> list(
        AudioDeviceDirection direction) = 0;
};

}  // namespace vocroz::audio_core
