// SPDX-License-Identifier: AGPL-3.0-only
//
// wasapi_loopback_input.h の実装。

#include "win/wasapi_loopback_input.h"

#include <algorithm>
#include <cstdio>
#include <new>
#include <span>
#include <system_error>
#include <utility>

#include <mmreg.h>

#include "vocroz/audio_core/constants.h"

#include "capture_constants.h"
#include "win/default_render_watcher.h"
#include "win/mmcss_registration.h"

namespace vocroz::capture::win {
namespace {

using audio_core::AudioDeviceError;
using Microsoft::WRL::ComPtr;

// ループバックのストリームのフラグ。レート、サンプル形式、チャンネル数の違いを OS に変換させる。
// SRC_DEFAULT_QUALITY は AUTOCONVERTPCM と組み合わせて、OS の標準品質のリサンプラーを使わせる。
constexpr DWORD kLoopbackStreamFlags =
    AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

// 32 ビット浮動小数点のサンプルのビット数。
constexpr WORD kFloat32Bits = 32;

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT。ksmedia.h の定義は GUID の実体を別にリンクする必要があるため、
// 値を直接書く（WAVE_FORMAT_IEEE_FLOAT を埋め込んだ標準の GUID）。
constexpr GUID kSubtypeIeeeFloat = {
    0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

// 内部フォーマット（48kHz、Float32、ステレオのインターリーブ）。
WAVEFORMATEXTENSIBLE make_loopback_format() noexcept {
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = static_cast<WORD>(kLoopbackChannels);
    format.Format.nSamplesPerSec = static_cast<DWORD>(audio_core::kSampleRate);
    format.Format.wBitsPerSample = kFloat32Bits;
    format.Format.nBlockAlign = static_cast<WORD>(kLoopbackChannels * sizeof(float));
    format.Format.nAvgBytesPerSec = format.Format.nSamplesPerSec * format.Format.nBlockAlign;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = kFloat32Bits;
    format.dwChannelMask = kStereoChannelMask;
    format.SubFormat = kSubtypeIeeeFloat;
    return format;
}

HRESULT activate(IMMDevice& device, ComPtr<IAudioClient>& client) {
    return device.Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(client.ReleaseAndGetAddressOf()));
}

}  // namespace

WasapiLoopbackInput::WasapiLoopbackInput(common::Logger& logger)
    : logger_(logger),
      // 停止の要求は、オーディオスレッドが見るまで立ったままにするので手動リセットにする。
      stop_event_(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

WasapiLoopbackInput::~WasapiLoopbackInput() {
    stop();
    if (thread_.joinable()) {
        thread_.join();
    }
}

common::Result<void, AudioDeviceError> WasapiLoopbackInput::start(
    audio_core::AudioCaptureCallback& callback, audio_core::AudioDeviceListener& listener) {
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
        thread_ = std::thread([this, &callback, &listener, started = std::move(started)]() mutable {
            run(callback, listener, started);
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

void WasapiLoopbackInput::stop() noexcept {
    if (stop_event_.get() != nullptr) {
        ::SetEvent(stop_event_.get());
    }
    // 抜去の通知の中から呼ばれたときに自分自身を待つとデッドロックするので、待たずに戻る。
    // スレッドの後始末は、次の start() かデストラクターで行う。
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) {
        thread_.join();
    }
}

std::size_t WasapiLoopbackInput::channel_count() const noexcept {
    return kLoopbackChannels;
}

void WasapiLoopbackInput::run(audio_core::AudioCaptureCallback& callback,
                              audio_core::AudioDeviceListener& listener,
                              StartPromise& started) noexcept {
    const ComApartment com;
    // オーディオのループより先に登録し、デバイスを開く処理の間も優先度を上げておく。
    const MmcssRegistration mmcss;
    // デバイスの周期ごとに WASAPI が立てるイベント。1 回の待機で 1 回ぶん消費するので自動リセット。
    const UniqueHandle audio_event(::CreateEventW(nullptr, FALSE, FALSE, nullptr));
    const UniqueHandle default_changed(::CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!com.is_ready() || audio_event.get() == nullptr || default_changed.get() == nullptr) {
        logger_.log(common::LogLevel::kError, "capture: cannot prepare the audio thread");
        started.set_value(AudioDeviceError::kInternal);
        return;
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        log_hresult(logger_, common::LogLevel::kError, "create device enumerator", hr);
        started.set_value(AudioDeviceError::kInternal);
        return;
    }
    // 切り替わりを見落とさないよう、既定のデバイスを取得する前に登録する。
    DefaultRenderWatcher watcher(default_changed.get());
    const bool watching = SUCCEEDED(enumerator->RegisterEndpointNotificationCallback(&watcher));
    if (!watching) {
        // 切り替わりに追従できないだけで、今の既定のデバイスの音は集められるので続ける。
        logger_.log(common::LogLevel::kWarning, "capture: cannot watch the default render device");
    }

    ComPtr<IMMDevice> device;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    Streams streams;
    std::optional<AudioDeviceError> error;
    if (FAILED(hr)) {
        log_hresult(logger_, common::LogLevel::kWarning, "get default render device", hr);
        error = AudioDeviceError::kDeviceNotFound;
    } else if (auto opened = open(*device.Get(), audio_event.get(), streams); !opened) {
        error = opened.error();
    } else if (hr = streams.capture->Start(); FAILED(hr)) {
        log_hresult(logger_, common::LogLevel::kError, "start loopback stream", hr);
        error = to_device_error(hr);
    }
    if (error) {
        if (streams.keep_alive) {
            streams.keep_alive->Stop();
        }
        if (watching) {
            enumerator->UnregisterEndpointNotificationCallback(&watcher);
        }
        started.set_value(*error);
        return;
    }
    started.set_value(std::nullopt);
    if (!mmcss.is_registered()) {
        logger_.log(common::LogLevel::kWarning, "capture: MMCSS registration failed");
    }

    hr = run_loop(streams, callback, audio_event.get(), default_changed.get());
    streams.capture->Stop();
    if (streams.keep_alive) {
        streams.keep_alive->Stop();
    }
    // 通知の中ではなく、ここ（通知を受けるスレッドの外）で解除する。
    if (watching) {
        enumerator->UnregisterEndpointNotificationCallback(&watcher);
    }
    if (FAILED(hr)) {
        log_hresult(logger_, common::LogLevel::kWarning, "loopback stream (device lost)", hr);
        // 停止の要求と同時に失敗した場合は、操作者が止めたのであって抜去ではないので通知しない。
        if (::WaitForSingleObject(stop_event_.get(), 0) != WAIT_OBJECT_0) {
            listener.on_device_lost();
        }
    }
}

common::Result<void, AudioDeviceError> WasapiLoopbackInput::open(IMMDevice& device,
                                                                 HANDLE audio_event,
                                                                 Streams& streams) {
    HRESULT hr = activate(device, streams.capture);
    if (FAILED(hr)) {
        log_hresult(logger_, common::LogLevel::kError, "activate audio client", hr);
        return common::make_unexpected(to_device_error(hr));
    }
    const WAVEFORMATEXTENSIBLE format = make_loopback_format();
    // バッファ長 0 は、イベント駆動の共有モードでエンジンの既定の周期を使う指定。
    hr = streams.capture->Initialize(AUDCLNT_SHAREMODE_SHARED, kLoopbackStreamFlags, 0, 0,
                                     &format.Format, nullptr);
    if (FAILED(hr)) {
        log_hresult(logger_, common::LogLevel::kError, "initialize loopback stream", hr);
        return common::make_unexpected(to_device_error(hr));
    }
    UINT32 buffer_frames = 0;
    hr = streams.capture->SetEventHandle(audio_event);
    if (SUCCEEDED(hr)) {
        hr = streams.capture->GetBufferSize(&buffer_frames);
    }
    if (SUCCEEDED(hr)) {
        hr = streams.capture->GetService(
            IID_PPV_ARGS(streams.capture_client.ReleaseAndGetAddressOf()));
    }
    if (FAILED(hr) || buffer_frames == 0) {
        log_hresult(logger_, common::LogLevel::kError, "configure loopback stream", hr);
        return common::make_unexpected(FAILED(hr) ? to_device_error(hr)
                                                  : AudioDeviceError::kInternal);
    }
    try {
        silence_.assign(static_cast<std::size_t>(buffer_frames) * kLoopbackChannels, 0.0F);
    } catch (const std::bad_alloc&) {
        return common::make_unexpected(AudioDeviceError::kInternal);
    }
    open_keep_alive(device, streams);

    char text[96];
    std::snprintf(text, sizeof(text), "capture: loopback opened (buffer %u frames%s)",
                  static_cast<unsigned>(buffer_frames), streams.keep_alive ? ", keep-alive" : "");
    logger_.log(common::LogLevel::kInfo, text);
    return {};
}

void WasapiLoopbackInput::open_keep_alive(IMMDevice& device, Streams& streams) {
    ComPtr<IAudioClient> client;
    HRESULT hr = activate(device, client);
    WAVEFORMATEX* raw_mix_format = nullptr;
    if (SUCCEEDED(hr)) {
        hr = client->GetMixFormat(&raw_mix_format);
    }
    const CoTaskMemPtr<WAVEFORMATEX> mix_format(raw_mix_format);
    // 書くのは常に無音（AUDCLNT_BUFFERFLAGS_SILENT）なので、変換の要らないミックスフォーマットで開く。
    if (SUCCEEDED(hr)) {
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, kKeepAliveBufferDuration, 0,
                                mix_format.get(), nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = client->GetBufferSize(&streams.keep_alive_frames);
    }
    if (SUCCEEDED(hr)) {
        hr = client->GetService(IID_PPV_ARGS(streams.render_client.ReleaseAndGetAddressOf()));
    }
    if (SUCCEEDED(hr)) {
        streams.keep_alive = client;
        // 開始の前にバッファを埋め、始まった直後に足りなくならないようにする。
        hr = feed_keep_alive(streams);
    }
    if (SUCCEEDED(hr)) {
        hr = client->Start();
    }
    if (FAILED(hr)) {
        // 再生中のアプリの音は集められるので、ループバックは止めない。何も再生していない間は
        // パケットが届かず、親機ではアプリ音声のストリップが無音のまま止まる。
        log_hresult(logger_, common::LogLevel::kWarning, "start keep-alive stream", hr);
        streams.keep_alive.Reset();
        streams.render_client.Reset();
    }
}

HRESULT WasapiLoopbackInput::run_loop(Streams& streams, audio_core::AudioCaptureCallback& callback,
                                      HANDLE audio_event, HANDLE default_changed) noexcept {
    // デバイスの周期を待つのはこのループだけで、待機は WASAPI のイベント駆動の仕組みそのもの。
    // コールバックの中では待たない（AGENTS.md 第 9 節）。
    const HANDLE handles[] = {stop_event_.get(), default_changed, audio_event};
    for (;;) {
        const DWORD wait = ::WaitForMultipleObjects(3, handles, FALSE, kStreamWatchdogMilliseconds);
        if (wait == WAIT_OBJECT_0) {
            return S_OK;
        }
        if (wait == WAIT_OBJECT_0 + 1) {
            logger_.log(common::LogLevel::kInfo, "capture: the default render device changed");
            return AUDCLNT_E_DEVICE_INVALIDATED;
        }
        if (wait != WAIT_OBJECT_0 + 2 && wait != WAIT_TIMEOUT) {
            return HRESULT_FROM_WIN32(::GetLastError());
        }
        // タイムアウトでも問い合わせる。抜去後はイベントが届かなくなるので、バッファの状態の
        // 問い合わせが返す AUDCLNT_E_DEVICE_INVALIDATED で抜去を検知する。
        HRESULT hr = feed_keep_alive(streams);
        if (SUCCEEDED(hr)) {
            hr = drain(streams, callback);
        }
        if (FAILED(hr)) {
            return hr;
        }
    }
}

HRESULT WasapiLoopbackInput::feed_keep_alive(Streams& streams) noexcept VOCROZ_NONBLOCKING {
    if (!streams.keep_alive) {
        return S_OK;
    }
    UINT32 padding = 0;
    HRESULT hr = streams.keep_alive->GetCurrentPadding(&padding);
    if (FAILED(hr) || padding >= streams.keep_alive_frames) {
        return hr;
    }
    const UINT32 frames = streams.keep_alive_frames - padding;
    BYTE* data = nullptr;
    hr = streams.render_client->GetBuffer(frames, &data);
    if (FAILED(hr)) {
        return hr;
    }
    // データは書かず、無音の印を付けて返す（エンジンが無音として扱う）。
    return streams.render_client->ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT);
}

HRESULT WasapiLoopbackInput::drain(
    Streams& streams, audio_core::AudioCaptureCallback& callback) noexcept VOCROZ_NONBLOCKING {
    // 1 回のイベントで複数のパケットがたまっていることがあるので、空になるまで読む。
    for (;;) {
        UINT32 packet_frames = 0;
        HRESULT hr = streams.capture_client->GetNextPacketSize(&packet_frames);
        if (FAILED(hr) || packet_frames == 0) {
            return hr;
        }
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        hr = streams.capture_client->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
        if (FAILED(hr)) {
            return hr;
        }
        if (hr == AUDCLNT_S_BUFFER_EMPTY) {
            return S_OK;
        }
        if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
            deliver_silence(callback, frames);
        } else {
            // ストリームは 32 ビット浮動小数点のステレオのインターリーブ形式で開いている。
            const std::span<const float> samples(reinterpret_cast<const float*>(data),
                                                 frames * kLoopbackChannels);
            callback.on_captured(audio_core::ConstInterleavedBuffer{samples, kLoopbackChannels});
        }
        hr = streams.capture_client->ReleaseBuffer(frames);
        if (FAILED(hr)) {
            return hr;
        }
    }
}

void WasapiLoopbackInput::deliver_silence(audio_core::AudioCaptureCallback& callback,
                                          UINT32 frames) noexcept VOCROZ_NONBLOCKING {
    const std::size_t capacity = silence_.size() / kLoopbackChannels;
    std::size_t remaining = frames;
    while (remaining > 0) {
        const std::size_t chunk = std::min(remaining, capacity);
        const std::span<const float> samples(silence_.data(), chunk * kLoopbackChannels);
        callback.on_captured(audio_core::ConstInterleavedBuffer{samples, kLoopbackChannels});
        remaining -= chunk;
    }
}

}  // namespace vocroz::capture::win
