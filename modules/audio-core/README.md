# audio-core

親機と子機のオーディオ処理の中核です。レイヤーはドメインで、`common` にだけ依存します（AGENTS.md 4.2）。
OS の API を呼ばないので、Linux の CI で単体テストと RealtimeSanitizer の検査を行えます。

## 責務

- 内部フォーマット（48kHz、Float32、標準ブロック 128 サンプル）。デバイスのコールバック長が違っても 128 サンプル以下に分けて処理する
- 音声グラフ（ソース → ストリップ → バス → リミッター → 出力）の記述、検証（ループの検出を含む）、レンダーグラフの構築
- ルーティング（Beta1、`docs/adr/0018-routing-graph.md`）: プリフェーダー / ポストフェーダーのセンドとセンドレベル、キュー / モニターバス、入力ごとの手動ディレイ、遅延ノードを通るフィードバック経路（承認があるときだけ）、1 台の出力デバイスの複数のチャンネルの組への出力
- グラフのスワップ（オーディオスレッド外で構築し、アトミックなポインター交換で差し替え、古いグラフはオーディオスレッド外で破棄）
- ミキサー（Alpha はモノラル 8 チャンネル以上 + Master の固定構成を、グラフの 1 構成として記述する）
- プラグインノードとプラグイン遅延補正（PDC、Beta4、`docs/adr/0024-plugin-node-and-pdc.md`）: `PluginProcessor` ポートを呼ぶノード（入力 0〜2、出力 1〜2。入力 0 は VSTi）、合流する経路にだけ遅延を足す補正、処理の失敗や NaN でそのノードを無音にする扱い。失敗の印（`PluginFault`）を渡せば、グラフを差し替えても無音を保ち、コントロールスレッドから失敗を読める（`docs/adr/0041-plugin-editor-and-failure-notice.md`）
- 録音タップと再生ノード（Beta2、`docs/adr/0031-recording-and-playback-nodes.md`）: オーディオスレッドはロックフリーのリングバッファを読み書きするだけで、ファイルへの書き出しと先読みはディスク I/O スレッドが行う。書き込みが遅れたら欠落として数えて無音で埋め、ライブ音声を止めない。音声ファイルの書き出しと読み込みのポート（`AudioFileWriter`、`AudioFileReader`）
- MIDI（Beta5、`docs/adr/0029-midi-and-vsti.md`）: PCM とは独立した MIDI の経路（MIDI ソース → プラグインノード）、タイムスタンプからブロック内のサンプルオフセットへの変換、届いた順の保証、経路ごとの発音中のノートの追跡と Panic（子機の切断、キューのあふれ、経路を外したグラフの差し替えで Note Off とサステインのオフを送る）
- 子機への返送モニター（Minus-Me、Beta1、`docs/adr/0036-return-monitor.md`）: 子機ごとに、その子機自身の入力と、そこから結線をたどって届くノード（自分の声を含むバス）を除いたミックスを、安全リミッターを通して返送タップ（`AudioSink` ポート）へ渡す
- SRTP 外部出力のストリームタップ（Beta2、`docs/adr/0040-srtp-external-output.md`）: 送り出すバスのポストフェーダーの音を、ステレオのまま `AudioStreamSink` ポートへ渡す。出力を持たないので、合奏の経路の音は変わらない
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
| `vocroz/audio_core/minus_me.h` | `add_minus_me_returns()`、`MinusMeReturn`、`MinusMeReport`（子機ごとの返送をグラフに足す） | 対象外（コントロールスレッドで記述する） |
| `vocroz/audio_core/feedback_analysis.h` | `analyze_feedback()`、`FeedbackReport`（ループに含まれるノードと、遅延のないループ） | 対象外 |
| `vocroz/audio_core/latency_compensation.h` | `analyze_latency()`、`LatencyReport`（ノードごとの遅延と結線ごとの補正） | 対象外 |
| `vocroz/audio_core/midi_event.h` | `MidiEvent`（ブロック内の MIDI メッセージ） | 呼び出し可能 |
| `vocroz/audio_core/midi_message.h` | チャンネルメッセージの検証（`is_channel_message()`）、Note On / Off の判定、Note Off とコントロールチェンジの組み立て | 呼び出し可能 |
| `vocroz/audio_core/midi_input_queue.h` | `MidiInputQueue`（MIDI の入力 1 系統のキュー。`push()` は生産者のスレッド、`request_panic()` は任意のスレッド） | `pull()` だけ呼び出し可能 |
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
| `vocroz/audio_core/ports/midi_input.h` | `MidiInput`（MIDI 入力ポート。`device` が JUCE で実装） | 対象外 |
| `vocroz/audio_core/ports/midi_input_callback.h` | `MidiInputCallback`（MIDI 入力のメッセージの受け取り） | 対象外 |
| `vocroz/audio_core/ports/midi_device_catalog.h` | `MidiDeviceCatalog`、`MidiDeviceInfo`（MIDI 入力ポートの一覧。`device` が実装） | 対象外 |
| `vocroz/audio_core/ports/audio_device_catalog.h` | `AudioDeviceCatalog`（デバイスの一覧。`device` が実装） | 対象外 |
| `vocroz/audio_core/audio_device_info.h` | `AudioDeviceInfo`、`AudioDeviceDirection` | 対象外 |
| `vocroz/audio_core/ports/audio_source.h` | `AudioSource`（`transport` のジッターバッファが実装。モノラルかステレオで、ストリップのチャンネル数はソースにそろえる。`docs/adr/0035-client-input-expansion.md`） | 呼び出し可能 |
| `vocroz/audio_core/ports/audio_sink.h` | `AudioSink`（返送タップの渡し先。`transport` の `MediaStreamEncoder` が実装） | 呼び出し可能 |
| `vocroz/audio_core/ports/audio_stream_sink.h` | `AudioStreamSink`（ストリームタップの渡し先。`transport` の `MediaStreamEncoder` が実装） | 呼び出し可能 |
| `vocroz/audio_core/ports/midi_source.h` | `MidiSource`、`MidiBlockClock`、`MidiPullResult`（`MidiInputQueue` が実装） | `pull()` だけ呼び出し可能 |
| `vocroz/audio_core/plugin_fault.h` | `PluginFault`（プラグインノードの失敗の印。`NodeSpec::make_plugin()` に渡す） | `raise()` と `raised()` は呼び出し可能 |
| `vocroz/audio_core/ports/plugin_processor.h` | `PluginProcessor`、`PluginBusLayout`、`PluginProcessContext`（`plugin-host` が実装） | `process()` だけ呼び出し可能 |
| `vocroz/audio_core/recording_track.h` | `RecordingTrack`（録音タップの書き込み先。ディスク I/O スレッドが `drain()` と `finish()` で書き出す） | `push()` だけ呼び出し可能 |
| `vocroz/audio_core/playback_track.h` | `PlaybackTrack`（再生ノードの読み込み元。ディスク I/O スレッドが `fill()` で先読みする） | `render()` だけ呼び出し可能 |
| `vocroz/audio_core/audio_file_error.h` | `AudioFileError` | 対象外 |
| `vocroz/audio_core/ports/audio_file_writer.h` | `AudioFileWriter`（`media` が実装） | 対象外（ディスク I/O スレッド） |
| `vocroz/audio_core/ports/audio_file_reader.h` | `AudioFileReader`（`media` が実装） | 対象外（ディスク I/O スレッド） |

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

### 使い方（プラグインの挿入）

```cpp
using namespace vocroz::audio_core;

// チャンネルへの挿入: ソース → プラグイン → ストリップ。バスへの挿入: バス → プラグイン → 後段。
std::shared_ptr<PluginProcessor> eq = ...;  // plugin-host が準備済みのものを返す
GraphDescription graph;
graph.nodes = {NodeSpec::make_source(NodeId{1}, source),
               NodeSpec::make_plugin(NodeId{2}, eq),  // この時点の遅延とチャンネル数を写す
               NodeSpec::make_strip(NodeId{3}, FaderId{0}, MeterId{0}),
               NodeSpec::make_source(NodeId{4}, other_source),
               NodeSpec::make_strip(NodeId{5}, FaderId{1}, MeterId{1}),
               NodeSpec::make_bus(NodeId{6}, FaderId{2}, MeterId{2}),
               NodeSpec::make_output(NodeId{7}, MeterId{3})};
graph.connections = {{NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}}, {NodeId{3}, NodeId{6}},
                     {NodeId{4}, NodeId{5}}, {NodeId{5}, NodeId{6}}, {NodeId{6}, NodeId{7}}};

// PDC: 5 → 6 の結線にだけ eq の遅延ぶんの補正が入る。出力ノードの値が聞こえる音の遅延。
if (auto latency = analyze_latency(graph)) { /* latency.value().node_latency_samples[6] */ }
(void)mixer.apply_graph(graph);
```

プラグインの遅延が変わったら、記述し直して適用します。処理が失敗したプラグインノードは、そのグラフの間は無音になります。

### 使い方（録音と再生）

```cpp
using namespace vocroz::audio_core;

// 録音: ストリップのフェーダーの前から録る（録音タップへのプリフェーダーの結線はセンドレベル不要）。
auto vocal = std::make_shared<RecordingTrack>(1);
std::unique_ptr<AudioFileWriter> vocal_file = ...;  // media が開いた WAV / FLAC
graph.nodes.push_back(NodeSpec::make_record_tap(NodeId{10}, vocal));
graph.connections.push_back({NodeId{2}, NodeId{10}, SendTap::kPreFader});
(void)mixer.apply_graph(graph);  // このブロックから録音が始まる（同じグラフのタップはそろって始まる）

// 再生: ファイルを Main バスへ流す。play() を呼ぶまで鳴らない。
auto bgm = std::make_shared<PlaybackTrack>(2);
std::unique_ptr<AudioFileReader> bgm_file = ...;  // media が開いたファイル
graph.nodes.push_back(NodeSpec::make_player(NodeId{11}, bgm));
graph.connections.push_back({NodeId{11}, NodeId{3}});
(void)mixer.apply_graph(graph);
bgm->play();

// ディスク I/O スレッドの周期処理。オーディオスレッドはこれを待たない。
if (auto written = vocal->drain(*vocal_file); !written) { /* AudioFileError を session へ */ }
if (auto read = bgm->fill(*bgm_file); !read) { /* AudioFileError を session へ */ }

// 録音の停止: タップを外したグラフを適用し、古いグラフが破棄されてから閉じる。
(void)mixer.apply_graph(graph_without_tap);
mixer.collect_garbage();
if (mixer.is_graph_settled()) { (void)vocal->finish(*vocal_file); }
```

書き込みが遅れて溢れたブロックは `dropped_frames()` に数え、ファイルには同じ長さの無音を書きます。トラック間の時間軸はずれません。
書き出しが失敗したトラック（`failure()`）は以降書き出しを止めますが、ライブ音声と他のトラックには影響しません。

### 使い方（MIDI で VSTi を鳴らす）

```cpp
using namespace vocroz::audio_core;

// MIDI のタイムスタンプと同じ基準の時計を渡すと、タイムスタンプをブロック内の位置に変換する。
Mixer mixer({}, clock);
auto keys = std::make_shared<MidiInputQueue>();  // MIDI の入力 1 系統（MIDI デバイス 1 台、子機 1 台）
std::shared_ptr<PluginProcessor> synth = ...;    // 入力 0 の VSTi（plugin-host が準備済みのもの）
GraphDescription graph;
graph.nodes = {NodeSpec::make_plugin(NodeId{1}, synth), NodeSpec::make_bus(NodeId{2}, FaderId{0}, MeterId{0}),
               NodeSpec::make_output(NodeId{3}, MeterId{1})};
graph.connections = {{NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}}};
graph.midi_sources = {{MidiSourceId{1}, keys}};
graph.midi_connections = {{MidiSourceId{1}, NodeId{1}}};  // PCM の結線とは独立
(void)mixer.apply_graph(graph);

// 生産者のスレッド（MIDI デバイスのコールバック、ネットワーク受信スレッド）。
(void)keys->push({clock.now(), {0x90, 60, 100}, 3});
// 子機の LOST、MIDI デバイスの抜去（session）。発音中のノートすべてに Note Off を送る。
keys->request_panic();
```

経路を外したグラフに差し替えると、その経路で発音中だったノートには、差し替え後の最初のブロックで Note Off を送ります。

### 使い方（子機への返送）

```cpp
using namespace vocroz::audio_core;

// 親機のグラフ（ストリップ 11〜13 → Master 20 → …）に、子機 A と B への返送を足す。
// 候補は Master へ入る信号。A の返送には A のストリップと、A を含むバスが入らない。
std::shared_ptr<AudioSink> to_a = ...;  // transport の MediaStreamEncoder
std::shared_ptr<AudioSink> to_b = ...;
const std::vector<NodeId> mix_inputs = {NodeId{11}, NodeId{12}, NodeId{13}};
const std::vector<MinusMeReturn> returns = {{{NodeId{11}}, to_a}, {{NodeId{12}}, to_b}};
if (auto added = add_minus_me_returns(graph, mix_inputs, returns); !added) { /* GraphError */ }
(void)mixer.apply_graph(graph);
```

返送はポストフェーダーで取るので、チャンネルミュートしたチャンネルは返送にも入りません。返送タップはステレオの入力を L と R の平均でモノラルにして、1 ブロックごとに `AudioSink::push()` を呼びます。

### 使い方（SRTP 外部出力へのストリームタップ）

```cpp
using namespace vocroz::audio_core;

// 送り出すバス（ここでは Master 20）のポストフェーダーから、ストリームタップ 30 へつなぐ。
std::shared_ptr<AudioStreamSink> external = ...;  // transport の MediaStreamEncoder（ステレオ）
graph.nodes.push_back(NodeSpec::make_stream_tap(NodeId{30}, external));
graph.connections.emplace_back(NodeId{20}, NodeId{30});
(void)mixer.apply_graph(graph);
```

ストリームタップは入力をステレオで受け、モノラルの経路は L と R に同じ音を入れます。1 ブロックごとに `AudioStreamSink::push_stereo()` を呼びます。出力を持たないので、ストリームタップから後段へはつなげません。

### グラフとフェーダーの関係

フェーダーとメーターの状態はグラフの外（`Mixer`）にあり、`FaderId` と `MeterId` で参照します。
グラフを差し替えても、フェーダーの値とランプの途中経過、メーターの値は続きます。
新しい参加者にストリップを割り当てるときは `reset_fader()` で初期状態（ミュート、0 dB）に戻します。

グラフから外した `AudioSource` は、古いグラフが破棄されるまで参照されています。
`is_graph_settled()` が true になれば、古いグラフは破棄済みです。

## テストダブル

`tests/fakes/` の `FakeAudioOutput`、`FakeAudioInput`、`FakeAudioSource`、`FakeAudioDeviceCatalog`、`FakePluginProcessor`（報告した遅延のぶん実際に遅らせ、受け取った MIDI を記録する）、`FakeAudioFileWriter`（書き込みの遅延と容量不足を模擬する）、`FakeAudioFileReader`（位置から値が決まるサンプルを返す）、`FakeMidiSource`、`FakeMidiInput`、`FakeMidiDeviceCatalog`、`FakeAudioSink`（受け取ったサンプルを記録する）は、CMake ターゲット `vocroz::audio_core_fakes` で他モジュールのテストからも使えます。

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
