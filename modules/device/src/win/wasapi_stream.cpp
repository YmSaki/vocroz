// SPDX-License-Identifier: AGPL-3.0-only
//
// wasapi_stream.h の実装。

#include "win/wasapi_stream.h"

#include <cstdio>
#include <system_error>
#include <utility>

#include "detail/stream_plan.h"
#include "device_constants.h"
#include "win/mmcss_registration.h"
#include "win/wasapi_endpoint.h"

namespace vocroz::device::win {
namespace {

using audio_core::AudioDeviceError;
using Microsoft::WRL::ComPtr;

// 通常の共有モードで、デバイスとのレートとサンプル形式の違いを OS に変換させるフラグ。
// SRC_DEFAULT_QUALITY は AUTOCONVERTPCM と組み合わせて、OS の標準品質のリサンプラーを使わせる。
constexpr DWORD kConvertedStreamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

// IAudioClient（か IAudioClient3）を有効化する。
template <typename Interface>
HRESULT activate(IMMDevice& device, ComPtr<Interface>& client) {
    return device.Activate(__uuidof(Interface), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(client.ReleaseAndGetAddressOf()));
}

// エンジンの周期を問い合わせる。IAudioClient3 がない（Windows 10 より前か、ドライバーが非対応）
// か、問い合わせに失敗したら std::nullopt を返し、通常の共有モードにする。
std::optional<detail::EnginePeriod> query_engine_period(IAudioClient3* client,
                                                        const WAVEFORMATEX& format) {
    if (client == nullptr) {
        return std::nullopt;
    }
    detail::EnginePeriod period;
    if (FAILED(client->GetSharedModeEnginePeriod(&format, &period.default_frames,
                                                 &period.fundamental_frames, &period.min_frames,
                                                 &period.max_frames))) {
        return std::nullopt;
    }
    return period;
}

}  // namespace

WasapiStream::WasapiStream(std::string endpoint_id, EDataFlow flow, std::size_t channels,
                           common::Logger& logger)
    : endpoint_id_(std::move(endpoint_id)),
      flow_(flow),
      channels_(channels),
      logger_(logger),
      // 停止の要求は、オーディオスレッドが見るまで立ったままにするので手動リセットにする。
      stop_event_(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

WasapiStream::~WasapiStream() {
    stop();
    if (thread_.joinable()) {
        thread_.join();
    }
}

common::Result<void, AudioDeviceError> WasapiStream::start(
    WasapiStreamPump& pump, audio_core::AudioDeviceListener& listener) {
    if (thread_.joinable()) {
        if (!finished_.load(std::memory_order_acquire)) {
            return common::make_unexpected(AudioDeviceError::kAlreadyStarted);
        }
        // 抜去で終わったスレッドの後始末をしてから開き直す。
        thread_.join();
    }
    if (stop_event_.get() == nullptr) {
        return common::make_unexpected(AudioDeviceError::kInternal);
    }
    ::ResetEvent(stop_event_.get());
    finished_.store(false, std::memory_order_release);

    StartPromise started;
    std::future<std::optional<AudioDeviceError>> result = started.get_future();
    try {
        thread_ = std::thread([this, &pump, &listener, started = std::move(started)]() mutable {
            run(pump, listener, started);
            finished_.store(true, std::memory_order_release);
        });
    } catch (const std::system_error&) {
        return common::make_unexpected(AudioDeviceError::kInternal);
    }
    if (const std::optional<AudioDeviceError> error = result.get()) {
        thread_.join();
        return common::make_unexpected(*error);
    }
    return {};
}

void WasapiStream::stop() noexcept {
    if (stop_event_.get() != nullptr) {
        ::SetEvent(stop_event_.get());
    }
    // 抜去の通知の中から呼ばれたときに自分自身を待つとデッドロックするので、待たずに戻る。
    // スレッドの後始末は、次の start() かデストラクターで行う。
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) {
        thread_.join();
    }
}

bool WasapiStream::is_active() const noexcept {
    return thread_.joinable() && !finished_.load(std::memory_order_acquire);
}

void WasapiStream::run(WasapiStreamPump& pump, audio_core::AudioDeviceListener& listener,
                       StartPromise& started) noexcept {
    const ComApartment com;
    // オーディオのループより先に登録し、デバイスを開く処理の間も優先度を上げておく。
    const MmcssRegistration mmcss;
    // デバイスの周期ごとに WASAPI が立てるイベント。1 回の待機で 1 回ぶん消費するので自動リセット。
    const UniqueHandle audio_event(::CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!com.is_ready() || audio_event.get() == nullptr) {
        logger_.log(common::LogLevel::kError, "device: cannot prepare the audio thread");
        started.set_value(AudioDeviceError::kInternal);
        return;
    }

    UINT32 buffer_frames = 0;
    auto client = open_client(audio_event.get(), buffer_frames);
    if (!client) {
        started.set_value(client.error());
        return;
    }
    IAudioClient& audio_client = *client.value().Get();
    HRESULT hr = pump.prepare(audio_client, buffer_frames);
    if (SUCCEEDED(hr)) {
        hr = audio_client.Start();
        if (FAILED(hr)) {
            pump.release();
        }
    }
    if (FAILED(hr)) {
        log_hresult(logger_, common::LogLevel::kError, "start stream", hr);
        started.set_value(to_device_error(hr));
        return;
    }
    started.set_value(std::nullopt);
    if (!mmcss.is_registered()) {
        logger_.log(common::LogLevel::kWarning, "device: MMCSS registration failed");
    }

    hr = run_loop(audio_client, pump, audio_event.get());
    audio_client.Stop();
    pump.release();
    if (FAILED(hr)) {
        log_hresult(logger_, common::LogLevel::kWarning, "audio stream (device lost)", hr);
        // 停止の要求と同時に失敗した場合は、操作者が止めたのであって抜去ではないので通知しない。
        if (::WaitForSingleObject(stop_event_.get(), 0) != WAIT_OBJECT_0) {
            listener.on_device_lost();
        }
    }
}

common::Result<ComPtr<IAudioClient>, AudioDeviceError> WasapiStream::open_client(
    HANDLE audio_event, UINT32& buffer_frames) {
    auto device = open_endpoint(endpoint_id_, flow_, logger_);
    if (!device) {
        return common::make_unexpected(device.error());
    }
    IMMDevice& endpoint = *device.value().Get();

    ComPtr<IAudioClient3> client3;
    ComPtr<IAudioClient> client;
    HRESULT hr = activate(endpoint, client3);
    if (SUCCEEDED(hr)) {
        client = client3;
    } else {
        client3.Reset();
        hr = activate(endpoint, client);
    }
    if (FAILED(hr)) {
        log_hresult(logger_, common::LogLevel::kError, "activate audio client", hr);
        return common::make_unexpected(to_device_error(hr));
    }

    WAVEFORMATEX* raw_mix_format = nullptr;
    hr = client->GetMixFormat(&raw_mix_format);
    const CoTaskMemPtr<WAVEFORMATEX> mix_format(raw_mix_format);
    if (FAILED(hr)) {
        log_hresult(logger_, common::LogLevel::kError, "get mix format", hr);
        return common::make_unexpected(to_device_error(hr));
    }
    const detail::MixFormat mix = summarize_format(*mix_format);
    // 作成後にデバイスのチャンネル数が変わったら、ポートが約束したチャンネル数を守れないので開かない。
    if (mix.channels != channels_) {
        logger_.log(common::LogLevel::kError, "device: channel count changed since creation");
        return common::make_unexpected(AudioDeviceError::kUnsupportedFormat);
    }
    const auto plan = detail::plan_stream(mix, query_engine_period(client3.Get(), *mix_format));
    if (!plan) {
        return common::make_unexpected(plan.error());
    }

    bool initialized = false;
    if (plan.value().mode == detail::StreamMode::kLowLatencyShared) {
        hr = client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                                  plan.value().period_frames, mix_format.get(),
                                                  nullptr);
        initialized = SUCCEEDED(hr);
        if (!initialized) {
            // 低遅延共有モードを断るドライバーがあるので、同じデバイスの通常の共有モードに切り替える。
            // 失敗した IAudioClient は再初期化できないので、有効化し直す。
            log_hresult(logger_, common::LogLevel::kWarning, "initialize low-latency stream", hr);
            client3.Reset();
            hr = activate(endpoint, client);
            if (FAILED(hr)) {
                log_hresult(logger_, common::LogLevel::kError, "activate audio client", hr);
                return common::make_unexpected(to_device_error(hr));
            }
        }
    }
    if (!initialized) {
        const WAVEFORMATEXTENSIBLE internal_format =
            make_internal_format(channels_, channel_mask_of(*mix_format));
        // バッファ長 0 は、イベント駆動の共有モードでエンジンの既定の周期を使う指定。
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, kConvertedStreamFlags, 0, 0,
                                &internal_format.Format, nullptr);
        if (FAILED(hr)) {
            log_hresult(logger_, common::LogLevel::kError, "initialize shared stream", hr);
            return common::make_unexpected(to_device_error(hr));
        }
    }

    hr = client->SetEventHandle(audio_event);
    if (SUCCEEDED(hr)) {
        hr = client->GetBufferSize(&buffer_frames);
    }
    if (FAILED(hr)) {
        log_hresult(logger_, common::LogLevel::kError, "configure stream", hr);
        return common::make_unexpected(to_device_error(hr));
    }

    char text[128];
    std::snprintf(text, sizeof(text), "device: stream opened (%s, period %u, buffer %u frames)",
                  initialized ? "low-latency shared" : "shared with conversion",
                  static_cast<unsigned>(plan.value().period_frames),
                  static_cast<unsigned>(buffer_frames));
    logger_.log(common::LogLevel::kInfo, text);
    return client;
}

HRESULT WasapiStream::run_loop(IAudioClient& client, WasapiStreamPump& pump,
                               HANDLE audio_event) noexcept {
    // デバイスの周期を待つのはこのループだけで、待機は WASAPI のイベント駆動の仕組みそのもの。
    // pump（オーディオスレッドのコールバック）の中では待たない（AGENTS.md 第 9 節）。
    const HANDLE handles[] = {stop_event_.get(), audio_event};
    for (;;) {
        const DWORD wait = ::WaitForMultipleObjects(2, handles, FALSE, kStreamWatchdogMilliseconds);
        if (wait == WAIT_OBJECT_0) {
            return S_OK;
        }
        if (wait != WAIT_OBJECT_0 + 1 && wait != WAIT_TIMEOUT) {
            return HRESULT_FROM_WIN32(::GetLastError());
        }
        // タイムアウトでも pump を呼ぶ。抜去後はイベントが届かなくなるので、バッファの状態の
        // 問い合わせが返す AUDCLNT_E_DEVICE_INVALIDATED で抜去を検知する。
        const HRESULT hr = pump.pump(client);
        if (FAILED(hr)) {
            return hr;
        }
    }
}

}  // namespace vocroz::device::win
