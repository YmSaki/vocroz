# webui

UI（`ui/`）を JUCE の `WebBrowserComponent`（Windows では WebView2）でホストするアダプターです。親機モードと子機モードで共有します。レイヤーはアダプターで、依存してよいのは `control-protocol` と `common` だけです（AGENTS.md 4.2）。
ブリッジの規則と WebView2 SDK の取得は `docs/adr/0005-ui-juce-webview.md` と `docs/adr/0020-ipc-pipe-and-webui-bridge.md` で決めています。

## 責務

- UI のウィンドウ（大きさと最小の大きさはコンポジションルートが渡す）
- 埋め込んだ `ui/dist` をリソースプロバイダーで配信する
- UI とのブリッジ（`control_protocol::MessageChannel` の実装）。JSON の文字列を中身を読まずに中継する
- 起動時の設定（`mode`、`lang`、`join`）を初期化データとして UI に渡す

メッセージの検証とコマンドの適用は持ちません（`pa-engine` の `session`、子機ロジック）。

## 公開 API

| ヘッダー | 内容 |
| --- | --- |
| `vocroz/webui/factories.h` | `make_juce_web_ui_host()`（`VOCROZ_WITH_JUCE` が ON のときだけ） |
| `vocroz/webui/web_ui_host.h` | `WebUiHost`、`WebUiHostOptions`、`WebUiWindowOptions`、`WebUiLaunchData`、`UiMode` |
| `vocroz/webui/ui_assets.h` | `UiAsset`、`UiAssetTable`（埋め込んだファイル） |
| `vocroz/webui/ui_resource.h` | `find_ui_resource()`、`mime_type_for()` |

### ブリッジの振る舞い

- UI からネイティブ側へはイベント `vocroz.to_engine`、ネイティブ側から UI へはイベント `vocroz.from_engine` を使います（`ui/src/ipc/juce-transport.ts` と同じ値）。
- `send()` はどのスレッドからでも呼べます。メッセージスレッドに渡してから WebView に送ります。ページの読み込み前と再読み込み中のメッセージは捨てます（UI は読み込み後に `hello` を送ります）。
- UI からの受信は、メッセージスレッドでリスナーに渡します。
- 埋め込んだ UI 以外のページへの移動は止めます。

### ui/dist の埋め込み

```cmake
vocroz_embed_ui_assets(remote_pa
  DIRECTORY "${PROJECT_SOURCE_DIR}/ui/dist"
  NAMESPACE vocroz::pa_desktop
  FUNCTION embedded_ui_assets)
```

```cpp
namespace vocroz::pa_desktop {
webui::UiAssetTable embedded_ui_assets();
}
```

configure の前に `ui` をビルドしておきます（`cd ui && npm ci && npm run build`）。

## テスト

`bridge_channel_test.cpp`、`ui_resource_test.cpp`、`embedded_assets_test.cpp` は JUCE に依存しないので、Linux の CI でも動きます。
WebView を含む往復は、コンポジションルートを組み立てたあとに実機で確かめます。

## 依存してよいモジュール

`control-protocol`、`common`。JUCE は `vocroz::juce`（`cmake/juce.cmake`）経由で `src/juce/` の中だけで使います。
