# ADR（設計判断記録）

| 番号 | 題名 | 状態 |
| --- | --- | --- |
| [0001](0001-key-exchange-noise-ik.md) | 鍵交換に Noise_IK を使う | 採用 |
| [0002](0002-opus-low-delay.md) | Opus を低遅延モードで使う | 採用（数値は P0 で実測して確定） |
| [0003](0003-ipc-named-pipe-json.md) | UI とエンジンの IPC は名前付きパイプ + JSON | 採用 |
| [0004](0004-side-by-side-updates.md) | 更新はバージョン別フォルダの切り替えで行う | 採用 |
| [0005](0005-ui-juce-webview.md) | UI は JUCE の WebBrowserComponent でホストし、親機と子機で 1 つのアプリを使う | 採用 |
| [0006](0006-invite-url-scheme.md) | 招待 URL は HTTPS のリンクとし、アプリの起動にはユーザー単位の URL スキームを使う | 採用 |
| [0007](0007-launch-interface.md) | 起動インターフェース（コマンドライン引数、単一インスタンス、終了コード） | 採用 |
| [0008](0008-runtime-behavior.md) | 実行時の振る舞い（電源、スケジューリング、クラッシュ、ファイアウォール、ディスプレイ） | 採用 |
| [0009](0009-beta-module-layout.md) | Beta1〜Beta5 の機能をどのモジュールに置くか | 採用 |
| [0010](0010-external-libraries.md) | 外部ライブラリはソースから固定バージョンで取得し、JSON には nlohmann/json を使う | 採用 |
| [0011](0011-ui-frontend-stack.md) | UI のフロントエンドは Vite + Preact + TypeScript で作り、フェイクエンジンで単独に開発する | 採用 |
| [0012](0012-rendezvous-protocol.md) | ランデブーサーバーのプロトコルと状態の持ち方 | 採用 |
| [0013](0013-installer-and-updater.md) | インストーラー、pa-updater、起動スタブの構成 | 採用 |
| [0014](0014-transport-wire-format.md) | 子機と親機の間の UDP パケットの形式と、ハンドシェイク後の暗号化 | 採用 |
| [0019](0019-reliable-control-layer.md) | 信頼制御層（制御メッセージの再送、キープアライブ、死活監視、再接続のバックオフ） | 採用 |

仕様書は RPC-SPEC-001（1.2-draft）。2026-11-07 にプロダクトオーナーが「UX を損なわない範囲で実装しやすい方法」を方針として承認した。
