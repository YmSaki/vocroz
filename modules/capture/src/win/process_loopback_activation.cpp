// SPDX-License-Identifier: AGPL-3.0-only
//
// process_loopback_activation.h の実装。

#include "win/process_loopback_activation.h"

#include <atomic>
#include <new>

#include <audioclientactivationparams.h>
#include <combaseapi.h>
#include <mmdeviceapi.h>
#include <objidl.h>

#include "capture_constants.h"
#include "win/com_utils.h"

namespace vocroz::capture::win {
namespace {

using Microsoft::WRL::ComPtr;

// ActivateAudioInterfaceAsync の完了を、イベントで呼び出し元のスレッドに知らせる。
// 完了の通知は OS のスレッドから届き、OS は通知の後もしばらく参照を持つことがある。
// 呼び出し元が待つのをやめた後（タイムアウト）にも届きうるので、寿命は参照カウントで決める。
// IAgileObject を実装しないと、OS はアパートメントをまたいで通知できず、活性化が失敗する。
class ActivationCompletion final : public IActivateAudioInterfaceCompletionHandler,
                                   public IAgileObject {
public:
    ActivationCompletion() noexcept
        // 完了は 1 回だけで、待つ側が見るまで立ったままにするので手動リセットにする。
        : done_(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ActivationCompletion(const ActivationCompletion&) = delete;
    ActivationCompletion& operator=(const ActivationCompletion&) = delete;

    [[nodiscard]] HANDLE done() const noexcept {
        return done_.get();
    }

    STDMETHODIMP QueryInterface(REFIID iid, void** object) noexcept override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (iid == __uuidof(IUnknown) ||
            iid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
            *object = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        } else if (iid == __uuidof(IAgileObject)) {
            *object = static_cast<IAgileObject*>(this);
        } else {
            *object = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() noexcept override {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    STDMETHODIMP_(ULONG) Release() noexcept override {
        const ULONG remaining = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    // 結果は呼び出し元が操作オブジェクトから受け取るので、ここでは完了を知らせるだけにする。
    STDMETHODIMP ActivateCompleted(
        IActivateAudioInterfaceAsyncOperation* /*operation*/) noexcept override {
        ::SetEvent(done_.get());
        return S_OK;
    }

private:
    ~ActivationCompletion() = default;

    std::atomic<ULONG> references_{1};
    UniqueHandle done_;
};

}  // namespace

HRESULT activate_loopback_excluding_this_process(ComPtr<IAudioClient>& client) noexcept {
    client.Reset();
    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    // 子機の返送モニターはこのプロセスが再生する。子プロセス（WebView2）の音も VOCROZ のものなので
    // 一緒に除く。
    params.ProcessLoopbackParams.TargetProcessId = ::GetCurrentProcessId();
    params.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT activation{};
    activation.vt = VT_BLOB;
    activation.blob.cbSize = sizeof(params);
    activation.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

    // 参照の 1 つはこの関数が持ち、最後に手放す。
    ComPtr<ActivationCompletion> completion;
    completion.Attach(new (std::nothrow) ActivationCompletion());
    if (!completion || completion->done() == nullptr) {
        return E_OUTOFMEMORY;
    }
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    HRESULT hr =
        ::ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
                                      &activation, completion.Get(), &operation);
    if (FAILED(hr)) {
        return hr;
    }
    if (::WaitForSingleObject(completion->done(), kActivationTimeoutMilliseconds) !=
        WAIT_OBJECT_0) {
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    HRESULT activated = E_FAIL;
    ComPtr<IUnknown> unknown;
    hr = operation->GetActivateResult(&activated, &unknown);
    if (FAILED(hr)) {
        return hr;
    }
    if (FAILED(activated)) {
        return activated;
    }
    return unknown.As(&client);
}

}  // namespace vocroz::capture::win
