# plugin-host

JUCE による VST3 のホストです。レイヤーはアダプターで、`session`、`audio-core`、`common` に依存してよいモジュールです（AGENTS.md 4.2）。
置き場所は `docs/adr/0009-beta-module-layout.md`、グラフのプラグインノードの規則は `docs/adr/0024-plugin-node-and-pdc.md` で決めました。

## 責務

- スキャン結果のスキーマ（`pa-plugin-scanner` が標準出力に書く JSON）。両側で同じコードを使います（AGENTS.md 4.4）
- スキャン用ヘルパーの起動、制限時間、異常終了の判定（`scan_in_helper()`）。不良プラグインがスキャン中に落ちても止まっても、呼び出し側のプロセスは巻き込まれません
- ヘルパーの中でのファイル 1 つのスキャン（`scan_in_process()`）
- プラグインの読み込みと、`audio-core` の `PluginProcessor` ポートの実装（`make_vst3_plugin_processor()`）。48kHz、128 サンプルで準備し、入力 0〜2、出力 1〜2 のメインバスだけを有効にします

グラフへの組み込みと PDC（`audio-core`）、挿入位置、スキャン結果のキャッシュ、読み込み不可のファイルの記録（`session`）は持ちません。

## 公開 API

| ヘッダー | 内容 | JUCE |
| --- | --- | --- |
| `vocroz/plugin_host/scan_messages.h` | `PluginDescription`、`ScanReport`、`serialize_scan_report()`、`parse_scan_report()` | 不要（Linux の CI でもテスト） |
| `vocroz/plugin_host/factories.h` | `scan_in_helper()`、`scan_in_process()`、`make_vst3_plugin_processor()` | 必要（`VOCROZ_WITH_JUCE` が ON のときだけ） |

### 使い方（pa-engine のコントロールスレッド）

```cpp
using namespace vocroz::plugin_host;
using namespace std::chrono_literals;

// プラグインファイル 1 つにつきヘルパーを 1 回起動する。
auto scanned = scan_in_helper(helper_exe, "C:/Program Files/Common Files/VST3/Reverb.vst3", 30s);
if (!scanned) { /* kCrashed、kTimedOut など: そのファイルを読み込み不可として記録する */ }

// 読み込みは JUCE のメッセージスレッド（pa-engine ではコントロールスレッド）で行う。
auto plugin = make_vst3_plugin_processor(scanned.value().plugins[0]);
if (plugin) {
    graph.nodes.push_back(vocroz::audio_core::NodeSpec::make_plugin(NodeId{10}, plugin.value()));
}
```

スキャンの制限時間は、実機で多くのプラグインを測ってから `session` 側で決めます（ADR-0009）。

## 設計上の注意

- JUCE のモジュールは GUI を含まない `juce_audio_processors_headless` を使っています。エディターのウィンドウを加えるときに `juce_audio_processors` に切り替えます
- JUCE のモジュールのソースはこのライブラリの中で 1 回だけコンパイルし、利用側にはインクルードパスと定義だけを渡します。利用側でもう一度リンクすると、シンボルが重複します
- プラグインの例外は捕まえて、以降は `kFailed` を返し続けます（グラフ側でそのノードは無音になります）。アクセス違反などの構造化例外は捕まえられないので、クラッシュ隔離はアウトオブプロセス実行（ADR-0009、未実装）で扱います
- `pa-plugin-scanner` は Windows のエラー報告のダイアログを出さないようにしています。出すとダイアログが閉じられるまで終わらず、制限時間まで待つことになるためです

## テスト

- `scan_messages_test`: スキーマの往復と、不良ヘルパーの出力の拒否
- `juce_helper_scan_test`: `tests/fake_plugin_scanner.cpp`（落ちる、止まる、壊れた出力を書くヘルパー）で、呼び出し側が落ちず、原因ごとの失敗が返ること
- `juce_plugin_processor_test`: JUCE の `AudioPluginInstance` のフェイクで、チャンネル構成と遅延の報告、その場での処理、例外の後に失敗を返し続けること

JUCE を使うテストは Windows の CI で実行します。Linux でも `-DVOCROZ_WITH_JUCE=ON` でビルドできます（headless のモジュールは GUI のライブラリを要求しません）。

## 依存してよいモジュール

`session`、`audio-core`、`common`（いまは `audio-core` と `common` だけを使います）
