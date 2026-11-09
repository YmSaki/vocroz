# 0034. JUCE のモジュールを 1 つのターゲットにまとめる

- 状態: 採用（2026-11-09）

## コンテキスト

JUCE のモジュールは、CMake でリンクしたターゲットごとにソースがコンパイルされる。
同じモジュールを 2 つの静的ライブラリが別々にコンパイルし、それが 1 つのバイナリに入ると、`juce_core` などのシンボルが重複してリンクが失敗する。

`docs/adr/0020-ipc-pipe-and-webui-bridge.md` で、この重複を避けるために、JUCE のモジュールを静的ライブラリ `vocroz::juce`（`cmake/juce.cmake`）に 1 回だけコンパイルすると決めた。
ところが、`plugin-host`（`docs/adr/0024-plugin-node-and-pdc.md`、`docs/adr/0032-plugin-catalog-and-loader.md`）は `juce_audio_processors_headless` を自前でコンパイルしている。
`vocroz::juce` は `juce_gui_extra` だけを持ち、`webui` だけが使っている。

これから JUCE を使うアダプターが増える。

- `device`: JUCE の `MidiInput` による MIDI デバイスの列挙、入力、抜去の検知（`docs/adr/0009-beta-module-layout.md`、`docs/adr/0029-midi-and-vsti.md`）。`juce_audio_devices` が要る。
  - 親機の MIDI 機器のために `pa-engine` へ入る。`pa-engine` には `plugin-host` も入る。
  - 子機の鍵盤のために `remote-pa` の子機モードへ入る。`remote-pa` には `webui` も入る。
- `plugin-host` のエディターのウィンドウ（`PluginEditorHost`）: GUI のモジュールが要る。`pa-engine` に入る。
- `media`: 音声ファイルの書き出しと読み込み。`juce_audio_formats` が要る。

アダプターごとに JUCE をコンパイルする今の形のままでは、`pa-engine` と `remote-pa` のどちらでもシンボルが重複する。

## 決定

- JUCE のモジュールは `vocroz::juce` の 1 つのターゲットにだけコンパイルする。JUCE を使うアダプターと `apps/` は、すべて `vocroz::juce` をリンクし、`juce::juce_*` のターゲットを直接リンクしない。
- `vocroz::juce` には、使うモジュールの和を PRIVATE でリンクする。今は次の 3 つ。
  - `juce_gui_extra`（`webui`）
  - `juce_audio_processors`（`plugin-host`。依存する `juce_audio_processors_headless` も入る）
  - `juce_audio_devices`（`device`）
- モジュールを足すときは、`cmake/juce.cmake` の `target_link_libraries` にだけ加える。`media` は `juce_audio_formats` を足す。
- JUCE の設定マクロ（`JUCE_PLUGINHOST_VST3`、`JUCE_WEB_BROWSER`、`JUCE_USE_CURL` など）は `vocroz::juce` に PUBLIC で 1 か所にだけ書く。同じバイナリの中で、モジュールのソースと利用側の定義を一致させるため。
- アダプターのソースは、使うモジュールのヘッダーだけを読む。たとえば `plugin-host` は今も `juce_audio_processors_headless` のヘッダーを読む。読むヘッダーを絞ることで、そのアダプターが JUCE のどこに依存しているかがソースから分かる。

## 理由

- **バイナリの組み合わせから独立させる**: アダプターごとにコンパイルする形では、どのアダプターとどのアダプターが同じバイナリに入るかで、リンクできるかが決まる。1 つにまとめれば、組み合わせを変えてもリンクの可否は変わらない。
- **JUCE の `MidiInput` を使い続けられる**: 重複を避けるために `device` の MIDI 入力を WinMM で書き直す案もあった。その案はビルド構成を変えずに済むが、ADR-0009 で選んだ JUCE の `MidiInput` から外れ、永続 ID と抜去の検知を自前で作ることになる。
- **GUI のモジュールはどのみち `pa-engine` に入る**: プラグインのエディターのウィンドウには `juce_gui_basics` が要る。`pa-engine` が headless のままでいられる期間は短い。

## 結果

- `pa-engine` と `pa-plugin-scanner` も、`juce_gui_extra` と WebView2 の静的ローダーをリンクする。使わないコードの分だけ実行ファイルが大きくなる。動作は変わらない。
- `plugin-host` のテストと `pa-plugin-scanner` は、ソースを変えずにビルドとテストが通る。
- Linux で `-DVOCROZ_WITH_JUCE=ON` にするときは、`plugin-host` だけを使う場合も WebKitGTK と GTK を要求する。CI の Linux のジョブは JUCE を使わないので影響しない。
- JUCE のモジュールの選び方を変えるときは、`cmake/juce.cmake` だけを見ればよい。
