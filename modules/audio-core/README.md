# audio-core

親機と子機のオーディオ処理の中核です。レイヤーはドメインで、`common` にだけ依存します（AGENTS.md 4.2）。
OS の API を呼ばないので、Linux の CI で単体テストと RealtimeSanitizer の検査を行えます。

## 責務

- 内部フォーマット（48kHz、Float32、標準ブロック 128 サンプル）。デバイスのコールバック長が違っても 128 サンプル以下に分けて処理する
- 音声グラフ（ソース → ストリップ → バス → リミッター → 出力）の記述、検証（ループの検出を含む）、レンダーグラフの構築
- ルーティング（Beta1、`docs/adr/0018-routing-graph.md`）: プリフェーダー / ポストフェーダーのセンドとセンドレベル、キュー / モニターバス、入力ごとの手動ディレイ、遅延ノードを通るフィードバック経路（承認があるときだけ）、1 台の出力デバイスの複数のチャンネルの組への出力
- グラフのスワップ（オーディオスレッド外で構築し、アトミックなポインター交換で差し替え、古いグラフはオーディオスレッド外で破棄）
- ミキサー（Alpha はモノラル 8 チャンネル以上 + Master の固定構成を、グラフの 1 構成として記述する）
- 5〜20ms のゲインランプ、安全リミッター、ピークと RMS のメーター算出
- 初期ミュート（すべてのフェーダーはミュートで始まる）と、安全ミュート（SAFETY MUTE）の受け口
- オーディオ入出力のポート（`AudioOutput`、`AudioInput`、デバイスの一覧の `AudioDeviceCatalog`）と、グラフのソースのポート（`AudioSource`）

デバイス制御（`device`）、ネットワークとジッターバッファ（`transport`）、ミュートの理由と解除の判断（`session`）は持ちません。

## 公開 API

| ヘッダー | 内容 | オーディオスレッド |
| --- | --- | --- |
| `vocroz/audio_core/constants.h` | `kSampleRate`、`kBlockSize`、`kMaxFaderGainDb`、遅延と出力チャンネルの上限 | 対象外 |
| `vocroz/audio_core/mixer.h` | `Mixer`、`MixerConfig`、`MixerError` | `render()` だけ呼び出し可能 |
| `vocroz/audio_core/graph_description.h` | `GraphDescription`、`NodeSpec`、`Connection`、`SendTap`、`NodeId`、`FaderId`、`MeterId`、`GraphError` | 対象外（コントロールスレッドで記述する） |
| `vocroz/audio_core/feedback_analysis.h` | `analyze_feedback()`、`FeedbackReport`（ループに含まれるノードと、遅延のないループ） | 対象外 |
| `vocroz/audio_core/fixed_mixer_layout.h` | `FixedMixerLayout`（Alpha の固定ミキサーの記述と識別子の割り当て） | 対象外 |
| `vocroz/audio_core/gain_ramp.h` | `GainRamp` | 呼び出し可能 |
| `vocroz/audio_core/gain.h` | `fader_db_to_linear()`、`linear_to_db()` | 呼び出し可能 |
| `vocroz/audio_core/meter_reading.h` | `MeterReading` | 対象外 |
| `vocroz/audio_core/audio_buffer.h` | `InterleavedBuffer`、`ConstInterleavedBuffer` | 呼び出し可能 |
| `vocroz/audio_core/audio_device_error.h` | `AudioDeviceError` | 対象外 |
| `vocroz/audio_core/realtime.h` | `VOCROZ_NONBLOCKING`（RealtimeSanitizer の検査対象にする注釈） | — |
| `vocroz/audio_core/ports/audio_output.h` | `AudioOutput`（`device` が実装） | 対象外 |
| `vocroz/audio_core/ports/audio_input.h` | `AudioInput`（`device`、`capture` が実装） | 対象外 |
| `vocroz/audio_core/ports/audio_render_callback.h` | `AudioRenderCallback`（`Mixer` が実装） | 呼び出し可能 |
| `vocroz/audio_core/ports/audio_capture_callback.h` | `AudioCaptureCallback` | 呼び出し可能 |
| `vocroz/audio_core/ports/audio_device_listener.h` | `AudioDeviceListener`（デバイスの抜去の通知） | 対象外 |
| `vocroz/audio_core/ports/audio_device_catalog.h` | `AudioDeviceCatalog`（デバイスの一覧。`device` が実装） | 対象外 |
| `vocroz/audio_core/audio_device_info.h` | `AudioDeviceInfo`、`AudioDeviceDirection` | 対象外 |
| `vocroz/audio_core/ports/audio_source.h` | `AudioSource`（`transport` のジッターバッファが実装） | 呼び出し可能 |

### 使い方（親機の固定ミキサー）

```cpp
using namespace vocroz::audio_core;

FixedMixerLayout layout(8);
Mixer mixer;  // すべてのフェーダーはミュートで始まる

// コントロールスレッド: 参加者のソースからグラフを記述して適用する。
std::vector<std::shared_ptr<AudioSource>> sources = ...;  // 参加者のいないチャンネルは null
if (auto result = mixer.apply_graph(layout.describe(sources)); !result) { /* GraphError */ }

// オペレーターの操作（session がコマンドを適用するときに呼ぶ）。
(void)mixer.set_fader_muted(layout.channel_fader(0), false);
(void)mixer.set_fader_gain_db(layout.master_fader(), -6.0F);

// 出力デバイスの抜去（AudioDeviceListener::on_device_lost() から直接呼んでよい）。
mixer.engage_safety_mute();

// コントロールスレッドの周期処理で、メーターを読み、古いグラフを破棄する。
const MeterReading level = mixer.read_meter(layout.channel_meter(0));
mixer.collect_garbage();
```

### 使い方（ルーティング）

```cpp
using namespace vocroz::audio_core;

// ソース → ストリップ（手動ディレイ 10ms）→ Main バス → 出力（チャンネル 1-2）と、
// ストリップ → プリフェーダーのセンド → キューバス → 出力（チャンネル 3-4）。
GraphDescription graph;
graph.nodes = {NodeSpec::make_source(NodeId{1}, source),
               NodeSpec::make_strip(NodeId{2}, FaderId{0}, MeterId{0}, 480),
               NodeSpec::make_bus(NodeId{3}, FaderId{1}, MeterId{1}),
               NodeSpec::make_output(NodeId{4}, MeterId{2}, 0),
               NodeSpec::make_bus(NodeId{5}, FaderId{2}, MeterId{3}),
               NodeSpec::make_output(NodeId{6}, MeterId{4}, 2)};
graph.connections = {{NodeId{1}, NodeId{2}},
                     {NodeId{2}, NodeId{3}},
                     {NodeId{3}, NodeId{4}},
                     {NodeId{2}, NodeId{5}, SendTap::kPreFader, FaderId{3}},  // センドレベル
                     {NodeId{5}, NodeId{6}}};

// ステージング: 適用せずに検証し、ループがあれば画面に示す。
const FeedbackReport loops = analyze_feedback(graph);
if (auto valid = mixer.validate_graph(graph); !valid) { /* GraphError を UI へ */ }
// 遅延ノードを通るループは、オペレーターが承認したら feedback_approved を立てて適用する。
(void)mixer.apply_graph(graph);
```

プリフェーダーのセンドには必ずセンドレベル（ミュートで始まるフェーダー）を付けます。新規参加者のストリップを割り当てるときは、チャンネルのフェーダーと同じくセンドレベルも `reset_fader()` で初期状態に戻します。

`Mixer` は `AudioRenderCallback` を実装しているので、`AudioOutput::start(mixer, listener)` でデバイスのコールバックにつなぎます。

### グラフとフェーダーの関係

フェーダーとメーターの状態はグラフの外（`Mixer`）にあり、`FaderId` と `MeterId` で参照します。
グラフを差し替えても、フェーダーの値とランプの途中経過、メーターの値は続きます。
新しい参加者にストリップを割り当てるときは `reset_fader()` で初期状態（ミュート、0 dB）に戻します。

グラフから外した `AudioSource` は、古いグラフが破棄されるまで参照されています。
`is_graph_settled()` が true になれば、古いグラフは破棄済みです。

## テストダブル

`tests/fakes/` の `FakeAudioOutput`、`FakeAudioInput`、`FakeAudioSource`、`FakeAudioDeviceCatalog` は、CMake ターゲット `vocroz::audio_core_fakes` で他モジュールのテストからも使えます。

```cmake
target_link_libraries(device_tests PRIVATE vocroz::device vocroz::audio_core_fakes)
```

```cpp
#include "fakes/fake_audio_output.h"
```

## RealtimeSanitizer

オーディオスレッドで実行する関数には、宣言と定義の `noexcept` の後に `VOCROZ_NONBLOCKING` を付けます。
CI の `Linux (RealtimeSanitizer)` のジョブは Clang 20 の `-fsanitize=realtime` でテストを実行し、この注釈を付けた関数の実行中にヒープ確保やロックがあればテストを失敗させます。
ローカルで実行するには Clang 20 以降が必要です。

```sh
cmake -S . -B build-rtsan -DCMAKE_CXX_COMPILER=clang++-20 -DCMAKE_C_COMPILER=clang-20 \
  "-DCMAKE_CXX_FLAGS=-fsanitize=realtime -Wno-function-effects" -DVOCROZ_WITH_JUCE=OFF
cmake --build build-rtsan --target audio_core_tests
ctest --test-dir build-rtsan/modules/audio-core --output-on-failure
```

## 依存してよいモジュール

`common`
