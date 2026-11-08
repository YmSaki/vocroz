// SPDX-License-Identifier: AGPL-3.0-only
//
// 1 台のデバイスの WASAPI のストリーム（共有モード、イベント駆動）の寿命を管理する。
// 出力と入力で共通の部分（専用スレッド、MMCSS、ストリームの開き方、開始と停止、抜去の検知）を持ち、
// バッファの読み書きは WasapiStreamPump に任せる。
#pragma once

#include <atomic>
#include <cstddef>
#include <future>
#include <optional>
#include <string>
#include <thread>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <wrl/client.h>

#include "vocroz/audio_core/audio_device_error.h"
#include "vocroz/audio_core/ports/audio_device_listener.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"

#include "win/com_utils.h"
#include "win/wasapi_stream_pump.h"

namespace vocroz::device::win {

/// 1 台のデバイスの WASAPI のストリーム。
/// ストリームの COM
/// オブジェクトはすべて専用のオーディオスレッドで作り、そのスレッドの中だけで使う。 スレッド安全性:
/// start()、stop()、is_active() はコントロールスレッドから呼ぶ。
class WasapiStream {
public:
    /// @param endpoint_id 開くデバイスのエンドポイント ID（UTF-8）
    /// @param flow        デバイスの向き（eRender か eCapture）
    /// @param channels    バッファのチャンネル数。デバイスのミックスフォーマットと同じ値
    /// @param logger      開始の失敗と抜去の詳細の出力先
    WasapiStream(std::string endpoint_id, EDataFlow flow, std::size_t channels,
                 common::Logger& logger);
    ~WasapiStream();
    WasapiStream(const WasapiStream&) = delete;
    WasapiStream& operator=(const WasapiStream&) = delete;

    /// オーディオスレッドを起動し、ストリームを開いて開始する。開始の成否が決まるまで待つ。
    /// @param pump     バッファの受け渡し。stop() が戻るまで生存させる
    /// @param listener 抜去の通知先。stop() が戻るまで生存させる
    /// @return 開始できたら成功。失敗の原因は AudioDeviceError
    /// エラー時の振る舞い: 失敗したら pump
    /// を呼ばない。別のデバイスや既定のデバイスへは切り替えない。
    [[nodiscard]] common::Result<void, audio_core::AudioDeviceError> start(
        WasapiStreamPump& pump, audio_core::AudioDeviceListener& listener);

    /// ストリームを停止し、オーディオスレッドの終了を待つ。開始前と抜去の後に呼んでもよい。
    /// 抜去の通知（オーディオスレッド）の中から呼ばれた場合は、終了を待たずに戻る。
    void stop() noexcept;

    /// オーディオスレッドが動いていれば true を返す。抜去の後は false。
    [[nodiscard]] bool is_active() const noexcept;

    /// バッファのチャンネル数を返す。
    /// スレッド安全性: どのスレッドからも呼び出し可能。
    [[nodiscard]] std::size_t channel_count() const noexcept {
        return channels_;
    }

private:
    using StartPromise = std::promise<std::optional<audio_core::AudioDeviceError>>;

    // オーディオスレッドの本体。開始の結果を promise で返し、停止か抜去までループする。
    void run(WasapiStreamPump& pump, audio_core::AudioDeviceListener& listener,
             StartPromise& started) noexcept;
    // デバイスを開き、計画（stream_plan）に従って IAudioClient を初期化する。
    [[nodiscard]] common::Result<Microsoft::WRL::ComPtr<IAudioClient>, audio_core::AudioDeviceError>
    open_client(HANDLE audio_event, UINT32& buffer_frames);
    // 停止の要求か、失敗（抜去を含む）までデバイスのイベントごとに pump を呼ぶ。
    // 失敗はすべて抜去として扱い、フェイルセーフのためにストリームを止める（AGENTS.md 1.2）。
    [[nodiscard]] HRESULT run_loop(IAudioClient& client, WasapiStreamPump& pump,
                                   HANDLE audio_event) noexcept;

    const std::string endpoint_id_;
    const EDataFlow flow_;
    const std::size_t channels_;
    common::Logger& logger_;
    UniqueHandle stop_event_;
    std::thread thread_;
    std::atomic<bool> finished_{false};
};

}  // namespace vocroz::device::win
