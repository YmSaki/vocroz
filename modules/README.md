# modules

共有ライブラリ群を置く場所です。各フェーズで追加します。
各モジュールの層、責務、依存してよい先は AGENTS.md 第 3 節と第 4 節が正本です。

| モジュール | 内容 | 追加するフェーズ |
| --- | --- | --- |
| `common` | 引数解析・バージョン表示などのユーティリティ、Result 型、SPSC キュー、`Clock` と `Logger` のポート | P0（追加済み） |
| `audio-core` | 48kHz Float32 の音声グラフ、ミキサー、ランプ、リミッター。Windows API に依存しない | P1 |
| `device` | WASAPI 入出力（低遅延共有 / 排他）、デバイス永続ID、抜去検知 | P1 |
| `transport` | パケット形式、Noise 鍵交換と AEAD、信頼制御層、Opus、ジッターバッファ、ドリフト補正 | P2 |
| `capture` | 子機のマイク入力キャプチャ | P2 |
| `session` | 招待、承認、再接続、接続と信号の状態モデル | P3 |
| `control-protocol` | UI とエンジン間のコマンド / イベント定義（名前付きパイプ + JSON） | P4 |
| `network` | Winsock の UDP ソケット、ランデブーサーバーとの HTTP 通信 | P2 |
| `ipc` | 名前付きパイプのクライアントとサーバー | P4 |
| `webui` | JUCE の WebBrowserComponent による UI のホストとブリッジ | P4 |
| `persistence` | 設定のアトミック保存とバックアップ、ログ、秘密情報の保存 | P5 |
| `update` | 更新マニフェストの検証、本番中のブロック、更新トランザクションとロールバックの判定、現行版の選択 | P5 |
