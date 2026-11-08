// SPDX-License-Identifier: AGPL-3.0-only
//
// WasapiStream が 1
// 回のデバイスのイベントごとに呼ぶ、向き（出力か入力か）ごとのバッファの受け渡し。
// ストリームの寿命（スレッド、開始と停止、抜去の検知）と、バッファの読み書きを分けるために置く。
#pragma once

#include <audioclient.h>
#include <windows.h>

#include "vocroz/audio_core/realtime.h"

namespace vocroz::device::win {

/// 向きごとのバッファの受け渡し。
class WasapiStreamPump {
public:
    virtual ~WasapiStreamPump() = default;

    /// ストリームの開始前に、サービス（IAudioRenderClient など）の取得とバッファの確保を行う。
    /// @param client        初期化済みの IAudioClient
    /// @param buffer_frames デバイスのバッファのフレーム数
    /// @return 失敗した HRESULT。成功なら S_OK
    /// スレッド安全性: ストリームのスレッドで、オーディオのループに入る前に呼ばれる。
    [[nodiscard]] virtual HRESULT prepare(IAudioClient& client, UINT32 buffer_frames) = 0;

    /// デバイスのイベント 1 回ぶんのサンプルを受け渡す。
    /// @param client 初期化済みの IAudioClient
    /// @return 失敗した HRESULT（抜去なら AUDCLNT_E_DEVICE_INVALIDATED）。成功なら S_OK
    /// スレッド安全性: オーディオスレッドから呼び出し可能。
    [[nodiscard]] virtual HRESULT pump(IAudioClient& client) noexcept VOCROZ_NONBLOCKING = 0;

    /// prepare() で取得したサービスを解放する。
    /// スレッド安全性: ストリームのスレッドで、オーディオのループを抜けた後に呼ばれる。
    virtual void release() noexcept = 0;
};

}  // namespace vocroz::device::win
