# capture

WASAPI のループバックで、子機のアプリ音声（PC で再生している伴奏など）を集めるアダプターのモジュールです。audio-core の `AudioInput` を実装します。
レイヤーはアダプターで、`audio-core` と `common` にだけ依存します（AGENTS.md 4.2）。
子機の入力としての扱い（ストリーム ID、申告、既定で送らないこと）は [ADR-0035](../../docs/adr/0035-client-input-expansion.md) にあります。

## 責務

- 既定の再生デバイス（コンソールの役割）の音を、共有モードのイベント駆動のループバックで集める
- 再生デバイスのチャンネル数とレートによらず、48kHz Float32 のステレオでコールバックに渡す（変換は OS に任せる）
- 何も再生していない間も無音を渡し続ける（同じデバイスに無音の再生ストリームを開いておく）
- 再生デバイスの抜去と、既定の再生デバイスの切り替わりで、ストリームを止めて `AudioDeviceListener::on_device_lost()` を 1 回呼ぶ。自動では開き直さない
- オーディオスレッドを MMCSS の「Pro Audio」に登録する（ADR-0008）

子機のマイク入力（`device`）、エンコード（`transport`）、アプリ音声を送るかどうかの判断（`session`）は持ちません。

## 公開 API

| ヘッダー | 内容 | 呼ぶスレッド |
| --- | --- | --- |
| `vocroz/capture/factories.h` | `make_wasapi_loopback_input()`（Windows だけ） | コントロール |

返した入力の `start()` と `stop()` はコントロールスレッドから、コールバックはループバックのオーディオスレッドから呼ばれます。

```cpp
using namespace vocroz;

auto input = capture::make_wasapi_loopback_input(logger).value();
// 既定の再生デバイスがなければ kDeviceNotFound。listener.on_device_lost() の後は開き直す。
if (auto started = input->start(processor, listener); !started) { /* AudioDeviceError */ }
```

## 内部の構成

| ファイル | 役割 |
| --- | --- |
| `src/win/wasapi_loopback_input.{h,cpp}` | ループバックと無音の再生ストリームの寿命、オーディオスレッド、パケットの受け渡し |
| `src/win/default_render_watcher.{h,cpp}` | 既定の再生デバイスの切り替わりの通知（`IMMNotificationClient`） |
| `src/win/com_utils.{h,cpp}`、`src/win/mmcss_registration.{h,cpp}` | COM と Win32 の RAII ラッパー、HRESULT の分類、MMCSS への登録 |

`device` にも同じ形の RAII ラッパーがありますが、アダプター同士は依存できない（AGENTS.md 4.1）ので、このモジュールで使う分だけを持ちます。

## テスト

WASAPI を呼ぶので、テストは Windows でだけビルドします。再生デバイスのない環境（CI のランナー）では、デバイスが要るテストをスキップします。
