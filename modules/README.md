# modules

共有ライブラリ群を置く場所です。各フェーズで追加します。
各モジュールの層、責務、依存してよい先は AGENTS.md 第 3 節と第 4 節が正本です。

| モジュール | 内容 | 追加するフェーズ |
| --- | --- | --- |
| `common` | 引数解析・バージョン表示などのユーティリティ、Result 型、SPSC キュー、`Clock` と `Logger` のポート | P0（追加済み） |
| `audio-core` | 48kHz Float32 の音声グラフ、ミキサー、ランプ、リミッター、メーター、オーディオ入出力のポート。Windows API に依存しない | P1（追加済み） |
| `device` | WASAPI 入出力（低遅延共有 / 排他）、デバイス永続ID、抜去検知 | P1 |
| `transport` | パケット形式、Noise 鍵交換と AEAD、信頼制御層、Opus、ジッターバッファ、ドリフト補正 | P2（Opus、ジッターバッファ、ドリフト補正、信号状態は追加済み） |
| `capture` | 子機のマイク入力キャプチャ | P2 |
| `session` | 招待、承認、再接続、接続と信号の状態モデル、コマンドの適用、子機と親機の制御メッセージ | P3（追加済み） |
| `control-protocol` | UI とエンジン間のコマンド / イベント定義（名前付きパイプ + JSON） | P4（追加済み） |
| `network` | Winsock の UDP ソケット、ランデブーサーバーとの HTTP 通信 | P2 |
| `ipc` | 名前付きパイプのクライアントとサーバー（ACL、長さ付き JSON、再接続） | P4（追加済み） |
| `webui` | JUCE の WebBrowserComponent による UI のホストとブリッジ、`ui/dist` の配信 | P4（追加済み） |
| `persistence` | 設定のアトミック保存とバックアップ、ログ、秘密情報の保存 | P5 |
| `media` | 音声ファイルの書き出し（WAV / FLAC）と読み込み。JUCE の AudioFormatManager を使う | Beta2 |
| `plugin-host` | VST3 のスキャン、読み込み、処理、エディターのウィンドウ。スキャン用ヘルパー `pa-plugin-scanner` とのメッセージ | Beta4 |
| `update` | 更新マニフェストの検証、本番中のブロック、更新トランザクションとロールバックの判定、現行版の選択 | P5 |
