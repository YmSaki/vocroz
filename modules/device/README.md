# device

WASAPI によるオーディオデバイスの入出力を担うアダプターのモジュールです。audio-core の `AudioOutput`、`AudioInput`、`AudioDeviceCatalog` を実装します。
レイヤーはアダプターで、`audio-core` と `common` にだけ依存します（AGENTS.md 4.2）。
実装手段と抜去の扱いの決定は [ADR-0023](../../docs/adr/0023-wasapi-device-io.md) にあります。

## 責務

- デバイスの指定はエンドポイント ID で行う。ID のデバイスがなければ失敗し、OS の既定のデバイスや別のデバイスを開かない
- 共有モードのイベント駆動で開く。デバイスが 48kHz Float32 なら `IAudioClient3` の低遅延共有モード（エンジンの最小周期）、そうでなければ通常の共有モードで、48kHz への変換を OS に任せる
- オーディオスレッドをストリームごとに作り、MMCSS の「Pro Audio」に登録する（ADR-0008）
- 抜去と無効化を検知したらストリームを止め、`AudioDeviceListener::on_device_lost()` を 1 回呼ぶ。挿し直しても自動では開き直さない
- 有効なデバイスの一覧（ID、表示名、チャンネル数、既定のデバイスかどうか）

Master のミュートと警告の表示（`session`、`Mixer::engage_safety_mute()`）、デバイスの選択の保存（`session`、`persistence`）は持ちません。

## 公開 API

| ヘッダー | 内容 | 呼ぶスレッド |
| --- | --- | --- |
| `vocroz/device/factories.h` | `make_wasapi_audio_device_catalog()`、`make_wasapi_audio_output()`、`make_wasapi_audio_input()`（Windows だけ） | コントロール |

ファクトリー関数は、ID のデバイスが有効で向きが合っているかを確かめ、デバイスのチャンネル数を決めてからアダプターを返します。
返したアダプターの `start()` と `stop()` はコントロールスレッドから、コールバックはそのデバイスのオーディオスレッドから呼ばれます。

```cpp
using namespace vocroz;

auto catalog = device::make_wasapi_audio_device_catalog(logger);
auto outputs = catalog->list(audio_core::AudioDeviceDirection::kOutput);

// 保存してあった ID で開く。なければ kDeviceNotFound で、既定のデバイスには切り替えない。
auto output = device::make_wasapi_audio_output(saved_output_id, logger);
if (!output) { /* 出力先の選び直しを促す */ }

// listener.on_device_lost() で mixer.engage_safety_mute() を呼び、session に抜去を知らせる。
if (auto started = output.value()->start(mixer, listener); !started) { /* AudioDeviceError */ }
```

## 内部の構成

| ファイル | 内容 |
| --- | --- |
| `src/detail/stream_plan.{h,cpp}` | ミックスフォーマットとエンジンの周期から、低遅延共有モードか通常の共有モードかを決める純粋関数。Linux でもテストする |
| `src/win/wasapi_stream.{h,cpp}` | 1 本のストリームの寿命（オーディオスレッド、開始と停止、抜去の検知）。出力と入力で共通 |
| `src/win/wasapi_render_pump.{h,cpp}`、`src/win/wasapi_capture_pump.{h,cpp}` | デバイスのイベントごとのバッファの受け渡し（出力、入力） |
| `src/win/wasapi_audio_output.{h,cpp}`、`src/win/wasapi_audio_input.{h,cpp}` | `AudioOutput`、`AudioInput` の実装 |
| `src/win/wasapi_audio_device_catalog.{h,cpp}` | `AudioDeviceCatalog` の実装 |
| `src/win/wasapi_endpoint.{h,cpp}` | ID によるエンドポイントの解決、ミックスフォーマットの読み取り、HRESULT の分類 |
| `src/win/com_utils.{h,cpp}`、`src/win/mmcss_registration.{h,cpp}`、`src/win/wide_string.{h,cpp}` | COM、ハンドル、MMCSS の RAII と、UTF-8 と UTF-16 の変換 |

## テスト

- `tests/stream_plan_test.cpp`: 開き方の判断（すべての OS）
- `tests/wasapi_device_test.cpp`: 実際の WASAPI を呼ぶテスト（Windows だけ）。デバイスがない環境では、デバイスの要るテストをスキップする
- 実機の抜去の確認は、`DISABLED_` の付いた手動テストで行う（`docs/testing/alpha-acceptance.md` の AT-A07）
