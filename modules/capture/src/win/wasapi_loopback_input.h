// SPDX-License-Identifier: AGPL-3.0-only
//
// WASAPI のループバックによる AudioInput の実装。
// 生成は factories.h の make_wasapi_loopback_input() から行う。
// 既定の再生デバイスの音（伴奏の再生アプリなど）を、子機のアプリ音声として集める。
// 子機の入力としての扱いは docs/adr/0035-client-input-expansion.md にある。
#pragma once

#include <atomic>
#include <cstddef>
#include <future>
#include <optional>
#include <thread>
#include <vector>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <wrl/client.h>

#include "vocroz/audio_core/audio_device_error.h"
#include "vocroz/audio_core/ports/audio_capture_callback.h"
#include "vocroz/audio_core/ports/audio_device_listener.h"
#include "vocroz/audio_core/ports/audio_input.h"
#include "vocroz/audio_core/realtime.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"

#include "win/com_utils.h"

namespace vocroz::capture::win {

/// 既定の再生デバイスのループバック。
/// ストリームの COM オブジェクトは、すべて専用のオーディオスレッドで作って使う。
/// スレッド安全性: start() と stop() はコントロールスレッドから呼ぶ。
/// channel_count() はどのスレッドからも呼び出し可能。
class WasapiLoopbackInput final : public audio_core::AudioInput {
public:
    /// @param logger 開始の失敗と抜去の詳細の出力先
    explicit WasapiLoopbackInput(common::Logger& logger);
    ~WasapiLoopbackInput() override;
    WasapiLoopbackInput(const WasapiLoopbackInput&) = delete;
    WasapiLoopbackInput& operator=(const WasapiLoopbackInput&) = delete;

    [[nodiscard]] common::Result<void, audio_core::AudioDeviceError> start(
        audio_core::AudioCaptureCallback& callback,
        audio_core::AudioDeviceListener& listener) override;

    /// ストリームを停止し、オーディオスレッドの終了を待つ。開始前と抜去の後に呼んでもよい。
    /// 抜去の通知（オーディオスレッド）の中から呼ばれた場合は、終了を待たずに戻る。
    void stop() noexcept override;

    [[nodiscard]] std::size_t channel_count() const noexcept override;

private:
    using StartPromise = std::promise<std::optional<audio_core::AudioDeviceError>>;

    // オーディオスレッドの中だけで使う COM オブジェクト。
    struct Streams {
        Microsoft::WRL::ComPtr<IAudioClient> capture;
        Microsoft::WRL::ComPtr<IAudioCaptureClient> capture_client;
        // 無音を流し続ける再生ストリーム。開けなければ空のまま（再生中のアプリの音は集められる）。
        Microsoft::WRL::ComPtr<IAudioClient> keep_alive;
        Microsoft::WRL::ComPtr<IAudioRenderClient> render_client;
        UINT32 keep_alive_frames = 0;
    };

    // オーディオスレッドの本体。開始の結果を promise で返し、停止か抜去までループする。
    void run(audio_core::AudioCaptureCallback& callback, audio_core::AudioDeviceListener& listener,
             StartPromise& started) noexcept;
    // 既定の再生デバイスをループバックで開き、無音の再生ストリームも開く。
    [[nodiscard]] common::Result<void, audio_core::AudioDeviceError> open(IMMDevice& device,
                                                                          HANDLE audio_event,
                                                                          Streams& streams);
    // 無音の再生ストリームを開いて開始する。失敗はログに残すだけにする。
    void open_keep_alive(IMMDevice& device, Streams& streams);
    // 停止の要求か、失敗（抜去と既定のデバイスの切り替わりを含む）までイベントごとに受け渡す。
    // 失敗はすべて抜去として扱い、フェイルセーフのためにストリームを止める（AGENTS.md 1.2）。
    [[nodiscard]] HRESULT run_loop(Streams& streams, audio_core::AudioCaptureCallback& callback,
                                   HANDLE audio_event, HANDLE default_changed) noexcept;
    // 再生ストリームのバッファの空きを無音で埋める。
    [[nodiscard]] static HRESULT feed_keep_alive(Streams& streams) noexcept VOCROZ_NONBLOCKING;
    // たまっているパケットをすべてコールバックに渡す。
    [[nodiscard]] HRESULT drain(
        Streams& streams, audio_core::AudioCaptureCallback& callback) noexcept VOCROZ_NONBLOCKING;
    // 無音のパケットのぶんだけ、無音のサンプルを渡す。
    void deliver_silence(audio_core::AudioCaptureCallback& callback,
                         UINT32 frames) noexcept VOCROZ_NONBLOCKING;

    common::Logger& logger_;
    UniqueHandle stop_event_;
    std::thread thread_;
    std::atomic<bool> finished_{false};
    // AUDCLNT_BUFFERFLAGS_SILENT のパケットはデータを読んではいけないので、代わりに渡す無音。
    // オーディオスレッドで確保しないよう、ストリームを開いたときにバッファ 1 つぶんを確保する。
    std::vector<float> silence_;
};

}  // namespace vocroz::capture::win
