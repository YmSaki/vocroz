# ui

JUCE の `WebBrowserComponent`（Windows のバックエンドは WebView2）上で動く TypeScript のフロントエンドです。親機モードと子機モードの両方の画面をここに置き、起動時にネイティブ側から受け取ったモードで画面を選びます。
P4 で追加します（TypeScript + Vite + Preact、メーターは Canvas 描画、文字列は日本語と英語の文字列リソースで管理）。
ビルド成果物（`dist/`）は `remote-pa` にバイナリリソースとして埋め込みます。構成と依存の規則は AGENTS.md 第 4.5 節と第 5.5 節、背景は `docs/adr/0005-ui-juce-webview.md` を参照してください。
