# AGENTS.md — VOCROZ 開発ルール

このファイルは、このリポジトリで設計、実装、レビューを行うすべての開発者と AI エージェントが従うルールです。
禁止事項の一覧ではなく、アーキテクチャ上の判断基準、責務の分割、ファイルの配置、依存関係の構築方法を、設計と実装でそのまま使える手順として定めます。
用語は専門用語をそのまま使います。言い換えによる意味のずれを避けるためです。
ルールを変えるときは、このファイルを更新する PR を出してから変えます。口頭やチャットだけで変えません。

**参照の順序**

- 新機能の設計: 第 2 節から第 7 節を順に読み、第 7 節の手順で配置を決める。
- 実装: 第 8 節から第 13 節を参照する。
- レビューと定期リファクタリング: 第 15 節の手順で、第 3 節から第 6 節と照合する。

## 1. 前提

- **プロダクト**: インターネット越しに複数の出演者の音声を集約し、1 台の親機（PA）でミキシングして出力する Windows 向けデスクトップアプリケーション。用途は合奏で、**低レイテンシが必須要件**。
- **仕様**: 製品要求・実装仕様書 RPC-SPEC-001（1.2-draft、[docs/spec/RPC-SPEC-001.md](docs/spec/RPC-SPEC-001.md)）。仕様書の節番号は改版でずれるため、このファイルとソースコードでは参照しない（第 10 節）。
- **設計判断**: [docs/adr](docs/adr/)（ADR: Architecture Decision Record）。仕様と ADR が矛盾する場合は ADR を優先する（ADR は仕様を更新した判断の記録）。
- **ライセンス**: AGPL-3.0-only。依存ライブラリは AGPL とライセンス互換のものに限る。

### 1.1 アーキテクチャ上の制約となる製品要件

次の 5 つは例外のない製品要件です。以降の構造は、これらを特定のレイヤーや機構で保証する前提で決めています。

| 要件 | 内容 | 保証する機構 |
| --- | --- | --- |
| オーディオ処理の独立性 | UI のハングやクラッシュでオーディオ処理を止めない。WebView で生の PCM データを扱わない | プロセス分離（2.2）。リアルタイムオーディオスレッドの分離（第 9 節） |
| 初期ミュート | 起動時、再起動時、新規参加時は、チャンネル、Master、子機の送信をすべてミュート状態で開始する | 初期状態を `session` と `audio-core` のドメインモデルで定義する。UI では決めない |
| エンジン主権（Single Source of Truth） | 正となる状態はエンジンだけが保持する。UI はコマンドを送り、ACK（`applied_revision`）を受けてから表示を更新する。フェーダーなど連続値のドラッグ中のみ楽観的更新（optimistic update）を許可する | コマンドとイベント（6.1、第 12 節） |
| 非モーダル UI | モーダルダイアログは不可逆操作の確認だけに使う | UI 設計（第 12 節） |
| 未実装機能の扱い | 未実装機能の UI 要素は「開発中」のトースト通知だけを出す。ダミーのウィンドウを開いたり状態を変更したりしない | UI 設計（第 12 節） |

### 1.2 トレードオフの優先順位

設計上の選択肢で迷ったら、上位を優先します。

1. **フェイルセーフ**: 未定義の状態、不正な入力、想定外のエラーでは、ミュートする側に倒す。出力デバイスを暗黙にフォールバックしない。
2. **低レイテンシ**: オーディオパスにブロッキングや追加のバッファリングを入れない。
3. **テスタビリティ**: OS、時刻、乱数、I/O への依存を差し替え可能にする（第 4 節）。
4. **シンプルさ**: 現時点で不要な汎用化や抽象化はしない（YAGNI）。ただし知識の重複は排除する（DRY、第 11 節）。

## 2. アーキテクチャ

### 2.1 アーキテクチャスタイル

C++ のコードは**ヘキサゴナルアーキテクチャ（Ports and Adapters）**で構成します。
ドメインロジック（オーディオ処理、トランスポートのプロトコル、セッション管理）を OS から分離し、Linux の CI 上で OS なしに単体テストでき、将来の macOS 対応ではアダプターの追加だけで済むようにするためです。

コードは次の 5 つのレイヤーのいずれかに属します。依存は外側のレイヤーから内側のレイヤーへだけ向けます（依存性逆転の原則、DIP）。

| レイヤー | 責務 | 配置 |
| --- | --- | --- |
| 共通基盤 | 横断的に使うユーティリティと、横断的関心事のポート（`Clock`、`Logger`）。Result 型 | `modules/common` |
| ドメイン | オーディオ処理、トランスポートのプロトコル、セッション管理などのドメインロジック。OS の API を呼ばない | `modules/audio-core`、`transport`、`control-protocol`、`session`、`update` |
| アダプター（インフラストラクチャ） | ドメインが定義したポートを、Win32、WASAPI、Winsock、JUCE などの外部ライブラリで実装する | `modules/device`、`capture`、`network`、`ipc`、`webui`、`persistence`、`plugin-host`、`media`、各アプリの `src/win/` |
| コンポジションルート | アダプターとドメインオブジェクトを生成し、依存性を注入し、プロセスを起動する | `apps/` |
| プレゼンテーション | 状態の描画と、ユーザー操作のコマンド化。JUCE の `WebBrowserComponent`（Windows のバックエンドは WebView2）上で動き、`control-protocol` のメッセージだけでネイティブ側と通信する | `ui/` |

ランデブーサーバー（`infra/rendezvous`）は C++ とは独立した Go のプログラムです。内部も同じ考え方で、外部インターフェースのパッケージとドメインのパッケージに分けます（5.5）。

### 2.2 プロセス構成

| プロセス | 責務 | 分離する理由 |
| --- | --- | --- |
| `pa-engine` | 親機のオーディオエンジン。受信、ジッターバッファ、Opus デコード、ミキシング、出力、セッション管理 | UI の障害をオーディオ処理に波及させないため |
| `remote-pa`（親機モード） | `webui` で UI をホストし、名前付きパイプ（`ipc`）経由で `pa-engine` を操作する | UI の障害をオーディオ処理に波及させないため |
| `remote-pa`（子機モード） | キャプチャ、Opus エンコード、送信と、`webui` でホストする子機の UI | 子機を単一プロセスで完結させ、配布物を 1 つにするため |
| `pa-updater` | セッション終了後にだけ起動するアップデーター | 本番中に更新処理を実行しないため |
| `pa-plugin-scanner` | `pa-engine` がプラグインファイル 1 つにつき 1 回起動する、VST3 のスキャン用ヘルパー。結果を標準出力の JSON で返す（`docs/adr/0009-beta-module-layout.md`） | 不良プラグインのクラッシュやハングで `pa-engine` を止めないため |
| `infra/rendezvous` | 招待コードの解決と、NAT 越えのためのアドレス交換 | メディアを中継せず、シグナリングだけを担うため |
| `vocroz`（起動スタブ） | 現行版の `remote-pa` を選んで起動し、すぐに終了する。ショートカットと `vocroz://` はこれを指す（`docs/adr/0013-installer-and-updater.md`） | 更新でファイルが入れ替わっても、ショートカットとレジストリの登録を変えずに済ませるため |

`remote-pa` の動作モードは起動ごとに固定します。招待 URL（URL スキーム `vocroz://`、`docs/adr/0006-invite-url-scheme.md`）か `--client` で起動したら子機モード、それ以外は親機モードで、起動中には切り替えません。2 つのモードはプロセス内の構成（`pa-engine` への接続か、自前のキャプチャと送信か）がまったく違い、実行中の切り替えは後始末の漏れによる音声の出しっぱなしなどの事故につながるためです。ウィンドウのサイズと最小サイズはモードごとにコンポジションルートで決め、子機モードは親機モードより小さくします（`docs/adr/0005-ui-juce-webview.md`）。

プロセス境界を越えるのはメッセージ（名前付きパイプ上の JSON、UDP パケット、HTTP）だけです。境界を越えるデータのスキーマは 1 か所で定義します（4.4）。

### 2.3 スレッドモデル

`pa-engine` と子機は次の 3 種類のスレッドで動作します。クラスを設計するときは、どのスレッドから呼ばれるかを先に決め、公開ヘッダーに明記します。

| スレッド | 責務 | 許可する処理 |
| --- | --- | --- |
| オーディオスレッド | デバイスのオーディオコールバック。ミキシングと出力、キャプチャとエンコード | 第 9 節のリアルタイム制約を満たす処理だけ |
| ネットワーク受信スレッド | UDP 受信、パケットの検証と復号、ジッターバッファへの投入 | ブロッキング I/O を含んでよい。オーディオスレッドへは SPSC キューで渡す |
| コントロールスレッド | IPC、セッションの状態遷移、設定の永続化、ロギング | 制約なし。オーディオスレッドへは SPSC キューかオーディオグラフのスワップで渡す |
| ディスク I/O スレッド（Beta2 以降） | 録音の書き出し、再生ファイルの読み込みとデコード | ブロッキング I/O を含んでよい。オーディオスレッドとはロックフリーの SPSC リングバッファだけで受け渡し、溢れたら欠落として記録してオーディオスレッドを待たせない |

スレッド間のデータの受け渡しは、6.1 のパターン（ロックフリー SPSC キュー、ダブルバッファリングによるグラフのスワップ）だけで行います。

## 3. 責務の分割

### 3.1 分割の判断基準

モジュール、クラス、ファイルの境界は、次の問いに順に答えて決めます。

1. **OS や外部ライブラリの API を呼ぶか。** 呼ぶ部分はアダプターに分離し、ドメインからはポート経由でだけ使う。
2. **どのスレッドで実行されるか。** オーディオスレッドで実行される部分と、それ以外で実行される部分は別のクラスにする。
3. **変更の理由は何か（単一責任の原則、SRP）。** 変更の理由が 2 つあれば分割する。例: ワイヤーフォーマットの変更と、再接続ポリシーの変更の両方で同じクラスを修正するなら、分割が誤っている。
4. **外部入力をパースするか。** パースとバリデーションは信頼境界の 1 か所（`wire/`、`control-protocol`）に集約し、内側には検証済みの型だけを渡す。
5. **状態と状態遷移を持つか。** 遷移規則はステートマシンとして 1 か所に集約し、副作用を持つ処理から分離する。

### 3.2 トップレベルのディレクトリ

```text
apps/          実行ファイル（エントリポイントとコンポジションルート）
  pa-desktop/      remote-pa  : 親機 UI のホスト。--client で子機モード
  client-desktop/  子機モード（remote-pa にリンクする静的ライブラリ）
  pa-engine/       pa-engine  : 親機のオーディオエンジン
  updater/         pa-updater : アップデーター
  launcher/        vocroz     : 現行版の remote-pa を起動する起動スタブ
  plugin-scanner/  pa-plugin-scanner : VST3 のスキャン用ヘルパー（Beta4）
modules/       ドメインとアダプターのライブラリ（3.3）
ui/            WebBrowserComponent 上で動く TypeScript のフロントエンド（親機と子機の両方の画面）
infra/         Go 製のランデブーサーバー
installer/     WiX Burn バンドルと MSI の定義、ビルドと署名のスクリプト
tools/         リリース作業用のツール（tools/release/: 更新マニフェストの鍵の生成と署名）
docs/adr/      ADR
docs/spec/     製品要求・実装仕様書
docs/testing/  実機確認の手順書（受け入れテストの手順、合否の基準、残す証拠）
cmake/         CMake の共通関数と、外部ライブラリの取得（cmake/<ライブラリ名>.cmake）
.github/       CI のワークフロー、Issue と PR のテンプレート、ラベルとマイルストーン
.githooks/     Git フック（pre-commit での自動フォーマット）
```

| ディレクトリ | 置くもの | 置かないもの |
| --- | --- | --- |
| `apps/` | エントリポイント、コンポジションルート | ビジネスロジック。条件分岐が必要になったらモジュールに移す |
| `modules/` | ドメインロジック、状態、アダプター | エントリポイント |
| `ui/` | 描画とコマンドの送信 | 状態の導出、生の PCM データ |
| `infra/` | シグナリング | メディアの中継 |
| `docs/adr/` | 設計判断のコンテキスト、決定、理由、結果 | 手順書、仕様書の転記 |
| `docs/testing/` | CI で確かめられない受け入れテストの、実機での手順、合否の基準、残す証拠 | 自動テストのコード（各モジュールの `tests/`）、確認で集めた証拠そのもの |
| `installer/` | WiX のソース、バンドルの画面のテーマ、インストーラーのビルドと署名のスクリプト | 更新の判断（`update`）、秘密鍵と証明書 |
| `tools/` | 開発者とリリース担当者が手元で使うツール | 製品に組み込むコード、秘密鍵 |

### 3.3 モジュールの責務

モジュールの追加や責務の変更は、先にこの表を更新する PR で行います（第 7 節）。

| モジュール | レイヤー | 責務 | 持たない責務（担当モジュール） |
| --- | --- | --- | --- |
| `common` | 共通基盤 | CLI 引数、文字列、時刻などのステートレスなユーティリティ。`Clock` と `Logger` のポート。Result 型 | 特定モジュールのドメイン知識 |
| `audio-core` | ドメイン | オーディオグラフ、ミキサー、ゲインランプ、リミッター、レベルメーターの算出。オーディオ入出力のポート。ルーティンググラフ、フィードバックループの検出、PDC、Minus-Me のミックス、録音タップと再生ノード、プラグインノード、MIDI の処理と Panic と、`PluginProcessor`、`AudioFileWriter`、`AudioFileReader`、`MidiInput` のポート（Beta、`docs/adr/0009-beta-module-layout.md`）。デバイスの一覧の `AudioDeviceCatalog` のポート（`docs/adr/0023-wasapi-device-io.md`） | デバイス制御（`device`）、ネットワーク（`transport`） |
| `transport` | ドメイン | ワイヤーフォーマット、Noise による鍵交換と AEAD（libsodium）、信頼制御層、Opus のエンコードとデコード、ジッターバッファ、クロックドリフト補正。ソケットのポート。親機から子機への下りストリーム、ステレオと複数ストリーム、MIDI 専用チャンネル（Beta、`docs/adr/0009-beta-module-layout.md`） | 参加者と承認の判断（`session`）、ソケットの実装（`network`） |
| `control-protocol` | ドメイン | UI とエンジン間のコマンドとイベントの DTO、JSON のシリアライズとデシリアライズ。メッセージを送受信する `MessageChannel` ポートと、2 つのチャンネルをつなぐ `ChannelRelay` | ビジネスロジック、状態（`session`） |
| `session` | ドメイン | 招待（招待 URL、`vocroz://` の引数、招待コードのパースと検証を含む）、承認、再接続、参加者とチャンネルストリップの状態、コマンドの適用。設定の永続化とランデブー通信のポート。ルーティングのステージングと適用、返送の設定、録音の状態、プラグインの一覧と挿入と、`PluginLoader`、`PluginScanner`、`PluginEditorHost` のポート（Beta、`docs/adr/0009-beta-module-layout.md`） | ワイヤーフォーマット（`transport`）、DSP（`audio-core`） |
| `device` | アダプター | WASAPI によるオーディオ入出力、エンドポイント ID の解決、デバイス抜去の検知。JUCE の `MidiInput` による MIDI デバイスの列挙（永続 ID）、入力、抜去の検知（Beta、`docs/adr/0009-beta-module-layout.md`）。デバイスの一覧（`AudioDeviceCatalog` の実装）と、子機のマイク入力にも使う `AudioInput` の実装（`docs/adr/0023-wasapi-device-io.md`） | ミュートなどのポリシー判断（`session`。`device` は検知をイベントとして通知するだけ） |
| `capture` | アダプター | 子機のマイク入力のキャプチャ。WASAPI のループバックによるアプリ音声の収集（Beta、`docs/adr/0009-beta-module-layout.md`）。Alpha の子機のマイク入力は `device` の `AudioInput` を使う（`docs/adr/0023-wasapi-device-io.md`） | エンコード（`transport`） |
| `network` | アダプター | Winsock の UDP ソケット、ランデブーサーバーとの HTTP 通信 | パケットのパース（`transport`） |
| `ipc` | アダプター | 名前付きパイプのクライアントとサーバー（`MessageChannel` の実装）。ACL によるアクセス制御 | メッセージの検証とコマンドの適用（`control-protocol`、`session`） |
| `webui` | アダプター | JUCE の `WebBrowserComponent` による UI のホスト、`ui/dist` を配信するリソースプロバイダー、ネイティブ関数とイベントのブリッジ（`MessageChannel` の実装）。モードごとのウィンドウ設定の適用。ルーティング画面などの独立したサブウィンドウのホスト（Beta、`docs/adr/0009-beta-module-layout.md`） | メッセージの検証とコマンドの適用（`pa-engine` の `session`、子機ロジック） |
| `persistence` | アダプター | 設定のアトミックな永続化、ログファイルへの出力、DPAPI による秘密情報の保護 | 設定値の意味づけ（`session`） |
| `plugin-host` | アダプター | JUCE による VST3 のスキャン、読み込み、処理、状態の保存と復元、エディターのウィンドウ（`PluginProcessor`、`PluginLoader`、`PluginScanner`、`PluginEditorHost` の実装）。`pa-plugin-scanner` との間のメッセージのスキーマ（Beta、`docs/adr/0009-beta-module-layout.md`） | グラフへの組み込みと PDC（`audio-core`）、挿入位置と状態の保存の判断（`session`） |
| `media` | アダプター | JUCE による音声ファイルの書き出し（WAV、FLAC）と、読み込みとデコード（`AudioFileWriter`、`AudioFileReader` の実装）（Beta、`docs/adr/0009-beta-module-layout.md`） | リングバッファとオーディオスレッドとの受け渡し（`audio-core`）、録音の状態と失敗の扱い（`session`） |
| `update` | ドメイン | セマンティックバージョン、更新マニフェストの検証（Ed25519、SHA-256）、更新の適用可否の判断（本番中のブロック）、更新トランザクションのステートマシンとロールバックの判定、現行版のポインターファイルの形式、起動するバージョンの選択。インストール先、起動確認、Authenticode、設定のバックアップのポート | ダウンロードとファイル操作の実装（`apps/updater/src/win/`）、セッションの状態（`session`） |

### 3.4 クラスとファイルの粒度

- 1 ファイルに 1 つの責務。C++ はクラスごとに `.h` と `.cpp` を 1 組作る。
- ファイルが 400 行、関数が 50 行、ネストが 3 段を超えそうになったら、3.1 の基準で分割を見直す。数値自体は閾値ではなく、複数の変更理由が混在していないかを確認する契機として使う。
- 公開 API は最小限にする。テストのためだけに公開しない（テストは公開 API 経由で行う）。

## 4. 依存関係の構築

### 4.1 依存の向きの決め方

- **依存は外側のレイヤーから内側のレイヤーへ向ける。** ドメインはアダプターもコンポジションルートも参照しない。
- **外部 I/O が必要になったら、利用する側がポートを定義する。** ドメインがオーディオを出力するなら、`audio-core` が `AudioOutput` ポートを定義し、`device` がそれを実装する。実装側（`device`）が定義側（`audio-core`）に依存するので、依存は外側から内側へ向く。
- **内側から外側へ通知するときは、内側がリスナーインターフェースを定義する（オブザーバーパターン）。** 例: `transport` がパケット到着を `session` に通知するなら、`transport` が `PacketListener` を定義し、`session` が実装して注入する。内側のモジュールが外側の型を include することはない。
- **アダプターのモジュールに依存してよいのはコンポジションルート（`apps/`）だけ。** アダプター同士、ドメインからアダプターへの依存は作らない。

### 4.2 モジュール間の依存の許可リスト

表にない依存は作りません。新しい依存が必要になったら、4.1 の基準で向きを確認し、この表を更新する PR を先に出します。

| モジュール | 依存してよいモジュール |
| --- | --- |
| `common` | なし |
| `audio-core` | `common` |
| `control-protocol` | `common` |
| `transport` | `audio-core`、`common` |
| `session` | `transport`、`control-protocol`、`audio-core`、`common` |
| `device`、`capture` | `audio-core`、`common` |
| `network` | `transport`、`session`、`common` |
| `ipc`、`webui` | `control-protocol`、`common` |
| `persistence` | `session`、`common` |
| `plugin-host` | `session`、`audio-core`、`common` |
| `media` | `audio-core`、`common` |
| `update` | `common` |
| `apps/*`（`client-desktop` を含む） | すべてのモジュール |

- 他のモジュールは公開ヘッダー（`include/vocroz/<module>/`）経由でだけ参照する。
- 依存は各モジュールの `CMakeLists.txt` の `target_link_libraries` に宣言する。`cmake/DependencyRules.cmake` がこの表と同じ許可リストを持ち、CMake の configure 時に各モジュールの依存を検査する。許可リストにない依存を宣言すると configure が失敗する。
- この表を変えるときは、`cmake/DependencyRules.cmake` も同じ PR で更新する。検査の対象は vocroz のモジュール同士の依存だけで、外部ライブラリの使用範囲（下の表）はレビューで確認する。

外部ライブラリの使用範囲も固定します。ライブラリの型がドメインに漏れると、差し替えもモックもできなくなるためです。

| ライブラリ | 使用してよい範囲 |
| --- | --- |
| libopus、Noise の実装 | `transport` |
| libsodium | `transport`、`update`（`update` は Ed25519 の署名検証と SHA-256 だけ。`docs/adr/0013-installer-and-updater.md`） |
| JUCE | アダプターレイヤーと `apps/`（ドメインの公開ヘッダーに JUCE の型を出さない） |
| WebView2（`WebBrowserComponent` 経由） | `modules/webui` |
| GoogleTest | 各モジュールと各アプリの `tests/` |
| nlohmann/json | `control-protocol`、`persistence` |
| Preact | `ui/`（UI の実行時に読み込む唯一のライブラリ。`docs/adr/0011-ui-frontend-stack.md`） |
| Vite、TypeScript、Vitest、jsdom、Testing Library、Prettier | `ui/` の開発時の依存（成果物に含めない） |

### 4.3 依存性の注入（C++）

依存性はコンポジションルート（`apps/<app>/src/composition_root`）で生成し、コンストラクターインジェクションで渡します。シングルトン、サービスロケーター、可変のグローバル変数は使いません。

1. コンポジションルートが、アダプターをファクトリー関数（`make_xxx()`）で生成し、`std::unique_ptr` で所有する。
2. ドメインオブジェクトを依存の順に生成し、メンバーとして所有する。
3. ドメインにはポートを**参照**で注入する。ライフタイムはコンポジションルートが保証する（メンバーは宣言の逆順に破棄されるので、利用側が先に破棄される）。
4. 所有権を移すのは、ファクトリー関数の戻り値と、ドメインがライフタイムを管理する必要のあるもの（例: スワップするオーディオグラフ）だけにする。

```cpp
// composition_root.h の構成例
class CompositionRoot {
 public:
  CompositionRoot();
  int run();

 private:
  // アダプター（外側）を先に宣言し、ドメインより後に破棄されるようにする。
  std::unique_ptr<audio_core::AudioOutput> audio_output_;
  std::unique_ptr<common::Clock> clock_;
  // ドメイン（内側）はポートを参照で受け取る。
  audio_core::Mixer mixer_;
  session::SessionManager session_;
};
```

### 4.4 プロセス境界とプログラム境界

境界を越えるデータのスキーマは、それぞれ 1 か所で定義します（Single Source of Truth）。両側に定義がある場合は、同じ PR で両方を変更します。

| 境界 | スキーマの定義 | 対になる定義 |
| --- | --- | --- |
| UI とネイティブ側（`webui`）、UI とエンジン（名前付きパイプ） | `control-protocol` | `ui/src/ipc/` のメッセージ型 |
| 子機と親機（UDP） | `transport/src/wire/` | なし（両側で同じコードを使う） |
| エンジンとランデブーサーバー（HTTP、STUN） | `infra/rendezvous/internal/httpapi/` | `session` と `network` のクライアント実装 |
| `pa-engine` と `pa-plugin-scanner`（標準出力の JSON） | `plugin-host` | なし（両側で同じコードを使う） |
| エンジンとランデブーサーバー（UDP のランデブーメッセージ） | `infra/rendezvous/internal/udpapi/wire.go` | `session` と `network` のクライアント実装 |

### 4.5 モジュール内、アプリ内、UI 内の依存

**モジュール内**

```text
include/vocroz/<module>/<名詞>.h  ←  src/<名詞>.cpp が実装する
include/vocroz/<module>/ports/    ←  src/ のドメインが利用し、src/win/ と tests/fakes/ が実装する
src/<名詞>.cpp（ドメイン）         →  ports/、state/、detail/、wire/、定数
src/state/                        →  定数のみ（ポートや他のクラスに依存しない純粋な遷移表）
src/wire/                         →  detail/、定数
src/detail/                       →  定数のみ
src/win/                          →  ports/、detail/、定数（ドメインのクラスに依存しない）
tests/                            →  公開ヘッダー、tests/fakes/
```

公開ヘッダーから `src/` を include しません。ドメインのクラスは `src/win/` を include せず、アダプターは `factories.h` 経由でコンポジションルートからだけ生成します。

**アプリ内**

```text
main.cpp  →  composition_root  →  各モジュールの公開ヘッダーと factories.h、そのアプリの src/win/
```

**UI 内**

```text
main.ts       →  screens/、state/、ipc/、i18n/（初期ロケールの設定）
screens/      →  components/、state/（購読）、ipc/（コマンド送信）、i18n/、utils/
components/   →  i18n/、utils/（state/ と ipc/ に依存しない Presentational Component）
state/        →  ipc/（メッセージ型のみ）、utils/
ipc/          →  utils/
i18n/、utils/  →  なし
```

`screens/shared/` は複数の画面で使う Container の部品（コマンドの送信、楽観的更新、トーストへの変換）で、`screens/` と同じ依存の規則に従う。
`ipc/fake-engine/` は開発とテストのためのフェイクエンジンで、`ipc/` の `Transport` を実装する。本番では読み込まない。

`components/` を Presentational Component（props だけで描画するコンポーネント）に限定するのは、親機と子機の両方の画面とテストで、props を渡すだけで再利用できるようにするためです。ストアの購読とコマンドの送信は Container の役割として `screens/` が担います。

## 5. 配置の地図

この節は「何を、どこに、どの形で置くか」の正本です。
レビューと定期リファクタリングは、この地図と第 3 節、第 4 節との照合で判断します。

### 5.1 配置表

| 対象 | 配置 | 形式 |
| --- | --- | --- |
| エントリポイント | `apps/<app>/src/main.cpp` | 「引数のパース → コンポジションルートの構築 → 実行」の 3 ステップだけ |
| 起動オプションの定義とパース | `apps/<app>/src/launch_options.{h,cpp}` | コマンドライン引数を検証済みの `LaunchOptions` 構造体に変換する純粋関数。矛盾や未知の引数はエラーとして返す（`docs/adr/0007-launch-interface.md`） |
| コンポジションルート | `apps/<app>/src/composition_root.{h,cpp}` | アプリに 1 つ。注入する依存ができた時点で作る。4.3 の手順で生成と注入を行う |
| ドメインロジック（ミキサー、ジッターバッファ、認可判定など） | 責務の合うモジュールの `src/<名詞>.{h,cpp}`。他モジュールに公開するものは `include/vocroz/<module>/<名詞>.h` | OS 非依存のクラス。外部 I/O にはポート経由でだけアクセスする |
| ポート | **利用する側**のモジュールの `include/vocroz/<module>/ports/<役割名>.h` | 純粋仮想クラスと virtual デストラクター。名前は役割を表す名詞（`AudioOutput`）。`I` などのプレフィックスは付けない。シグネチャにはドメインの型だけを使う |
| リスナー（内側から外側への通知） | 通知する側のモジュールの `include/vocroz/<module>/ports/<名詞>_listener.h` | ポートと同じ形式 |
| 横断的関心事のポート（`Clock`、`Logger`） | `modules/common/include/vocroz/common/ports/` | 複数のレイヤーが使うものだけ。単一モジュールだけが使うポートはそのモジュールに置く |
| アダプター | アダプターレイヤーのモジュールの `src/win/<技術名>_<役割名>.{h,cpp}`（Win32 を直接呼ぶ実装）か `src/juce/<技術名>_<役割名>.{h,cpp}`（JUCE による実装）。単一アプリだけが使うものはそのアプリの `src/win/` | クラス名は技術名をプレフィックスにする（`WasapiAudioOutput`）。具象クラスは公開せず、`include/vocroz/<module>/factories.h` のファクトリー関数（`make_wasapi_audio_output()`）だけを公開する |
| ステートマシン | 状態を持つモジュールの `src/state/<名詞>_state_machine.{h,cpp}` | 6.1 の形式 |
| ワイヤーフォーマットのパースとシリアライズ | `modules/transport/src/wire/` | バイト列から検証済みの構造体を返す純粋関数 |
| UI とエンジン間のコマンドとイベント | `modules/control-protocol` | DTO と JSON のシリアライズとデシリアライズだけ |
| 定数 | 利用するモジュールの `src/<module>_constants.h`。複数モジュールが使うものは `include/vocroz/<module>/constants.h` | `constexpr`。値の根拠をコメントに書く |
| 汎用ユーティリティ | `modules/common` | ステートレスな自由関数 |
| モジュール内ユーティリティ | `src/detail/<名詞>_utils.{h,cpp}` | `vocroz::<module>::detail` 名前空間 |
| 翻訳単位内だけのヘルパー | その `.cpp` の無名名前空間 | ヘッダーに出さない |
| テストダブル（フェイク） | ポートを定義したモジュールの `tests/fakes/fake_<役割名>.h` | `Fake` プレフィックス（`FakeClock`）。決定的に動作する |
| UI の画面（Container） | `ui/src/screens/<画面名>/` | その画面だけで使うコンポーネントも同じディレクトリに置く |
| UI の共通コンポーネント（Presentational） | `ui/src/components/<コンポーネント名>/` | 複数の画面で使うもの。props だけで描画する |
| UI のストア | `ui/src/state/` | エンジンのスナップショットとイベントを保持する |
| UI の IPC クライアント | `ui/src/ipc/` | コマンドの送信と、スナップショットとイベントの受信 |
| UI の文字列リソース | `ui/src/i18n/ja.json`、`ui/src/i18n/en.json` | キーで参照する |
| UI の画面間で共有する Container の部品 | `ui/src/screens/shared/` | 2 つ以上の画面で同じ知識として使うフックと関数だけ |
| UI の開発用フェイクエンジン | `ui/src/ipc/fake-engine/fake-<モード>-engine.ts` | `Transport` を実装する。JUCE の外で開いたときだけ動的に読み込む |
| UI の配色と基本の書式 | `ui/src/theme.css` | CSS 変数で定義し、各コンポーネントの CSS が参照する |
| UI のテストダブル | 差し替える抽象と同じディレクトリの `fake-<名前>.ts` | `Fake` プレフィックス（`FakeTimers`）。決定的に動作する |
| ランデブーサーバーの機能 | `infra/rendezvous/internal/<機能名>/` | 5.5 の形式 |
| 単体テスト | C++ は対象と同じモジュールの `tests/<対象>_test.cpp`。TypeScript と Go は対象と同じディレクトリ | 対象ファイル 1 つにテストファイル 1 つ |
| ADR | `docs/adr/NNNN-<題名>.md` | コンテキスト、決定、理由、結果 |
| 外部ライブラリの取得 | `cmake/<ライブラリ名>.cmake` | 1 ライブラリに 1 ファイル。バージョンをハッシュかコミットで固定する（`docs/adr/0010-external-libraries.md`） |
| 外部ライブラリのリンク確認テスト | `cmake/link-tests/<ライブラリ名>_link_test.cpp` | ビルドとリンクと最小限の呼び出しだけを確かめる。使い方のテストは使うモジュールの `tests/` に書く |

### 5.2 配置の決定手順

上から順に判定し、最初に該当した場所に置きます。

1. OS、デバイス、ネットワークの API、JUCE を直接呼ぶか → アダプターレイヤーのモジュールの `src/win/`（Win32）か `src/juce/`（JUCE）。生成は `factories.h`
2. 外部（ネットワーク、IPC、ファイル）からの入力をパースするか → 信頼境界のパーサー（`transport/src/wire/`、`control-protocol`）
3. 状態遷移の規則か → 状態を持つモジュールの `src/state/`
4. 複数モジュールが同じ知識として使い、特定モジュールのドメイン知識を必要としないか → `modules/common`
5. 単一モジュール内の複数箇所が同じ知識として使うか → そのモジュールの `src/detail/`
6. それ以外 → 3.3 の責務表で責務の合うモジュールの、責務の合うクラス

### 5.3 C++ モジュールのディレクトリ構成

ファイルは役割ごとのディレクトリに分けます。
同じ役割のファイルは、どのモジュールでも同じ名前のディレクトリに置きます。ディレクトリ名から役割が特定でき、配置の正誤をレビューで即座に判定できるようにするためです。
空のディレクトリは作らず、該当するファイルができた時点で作ります。

```text
modules/<module>/
  CMakeLists.txt                 ターゲット vocroz_<module> と ALIAS vocroz::<module>
  README.md                      責務、公開 API、依存してよいモジュール
  include/vocroz/<module>/       公開ヘッダー（他モジュールに公開するものだけ）
    <名詞>.h                       公開するクラスと値型（例: mixer.h、channel_state.h）
    ports/<役割名>.h               ポートとリスナー（例: ports/audio_output.h）
    factories.h                    アダプターのファクトリー関数（アダプターレイヤーのモジュールだけ）
    constants.h                    複数モジュールが使う定数
  src/
    <名詞>.{h,cpp}                 ドメインロジックのクラスと、公開ヘッダーの実装
    <module>_constants.h           モジュール内だけで使う定数
    state/<名詞>_state_machine.{h,cpp}
    detail/<名詞>_utils.{h,cpp}
    wire/                          ワイヤーフォーマットのパースとシリアライズ（信頼境界のモジュールだけ）
    win/<技術名>_<役割名>.{h,cpp}  Win32 を直接呼ぶアダプター（例: win/wasapi_audio_output.cpp）
    juce/<技術名>_<役割名>.{h,cpp} JUCE によるアダプター（例: juce/juce_web_ui_host.cpp）。JUCE は OS 非依存なので win/ と分ける
  tests/
    <対象>_test.cpp
    fakes/fake_<役割名>.h
```

`CMakeLists.txt` は `modules/common/CMakeLists.txt` と同じ順序（ライブラリ定義 → ALIAS → include ディレクトリ → 依存 → 警告 → テスト）で書きます。

### 5.4 アプリのディレクトリ構成

```text
apps/<app>/
  CMakeLists.txt
  src/
    main.cpp                       エントリポイント（引数のパース → コンポジションルートの構築 → 実行）
    launch_options.{h,cpp}         起動オプションの定義とパース
    composition_root.{h,cpp}       依存性の生成と注入
    win/                           そのアプリだけが使う Windows のアダプター
  tests/                           起動のスモークテスト
```

`client-desktop` は `remote-pa` にリンクする静的ライブラリなので、モジュールと同様に `include/vocroz/client/` を持ちます。
UI のホスト（`webui`）は親機モードと子機モードで共有します。`client-desktop` は `pa-desktop` に依存できないため、ホストはアプリ内ではなくモジュールに置いています。

| モード | コンポジションルートでの接続 |
| --- | --- |
| 親機モード（`pa-desktop`） | `webui` の `MessageChannel` と `ipc` のクライアントを `ChannelRelay` でつなぎ、`pa-engine` へ中継する |
| 子機モード（`client-desktop`） | `webui` の `MessageChannel` を、プロセス内の子機ロジック（`session`、`transport`、`capture`）につなぐ |
| `pa-engine` | `ipc` のサーバーを `session` につなぐ |

### 5.5 UI とランデブーサーバーのディレクトリ構成

C++ はレイヤー単位でモジュールを分けます（package by layer）が、UI は画面単位でディレクトリを分けます（package by feature）。UI はビジネスロジックを持たず、変更の単位がほぼ画面と一致するためです。

```text
ui/src/
  main.ts                          エントリポイント。ネイティブ側から受け取ったモードで画面を選び、IPC クライアントとストアを接続してマウントする
  ipc/                             IPC クライアント（JUCE のネイティブ関数とイベントのラッパー）とメッセージ型
  state/                           ストア。スナップショットとイベントを保持し、画面に配信する
  screens/<画面名>/                 画面単位の Container（例: mixer/、client/、settings/）
  components/<コンポーネント名>/      共通の Presentational Component（例: fader/、meter/、toast/）
  i18n/                            ja.json、en.json と文字列リソースのローダー
  utils/                           ステートレスなヘルパー関数
ui/dist/                           ビルド成果物（Git 管理外）。remote-pa にバイナリリソースとして埋め込む
```

- 画面とコンポーネントは `<名前>.tsx`（PascalCase）を中心に、スタイル（`<名前>.css`）とテスト（`<名前>.test.ts`）を同じディレクトリに置く（コロケーション）。
- それ以外のファイル名は kebab-case（例: `engine-client.ts`）。

ランデブーサーバーも、外部インターフェースのパッケージ（`httpapi`、`stun`）とドメインのパッケージ（`invite` など）に分け、前者から後者へだけ依存させます。

```text
infra/rendezvous/
  cmd/rendezvous/main.go           エントリポイント兼コンポジションルート
  internal/<機能名>/                機能単位のパッケージ
    <機能名>.go                      パッケージの中心となる型と関数
    <責務>.go                        それ以外は責務ごとのファイル（例: handler.go、store.go）
    <ファイル名>_test.go             テストは同じパッケージ
```

### 5.6 ファイルとクラスの記述順序

- ファイル: SPDX ヘッダーとファイルの説明 → include（clang-format の順序） → 名前空間 → 宣言。
- クラス: `public` の型 → コンストラクターとデストラクター → `public` メンバー関数 → `private` メンバー関数 → `private` データメンバー。

## 6. 場面ごとの設計

### 6.1 採用する設計パターン

場面ごとに使うパターンを固定します。ここにない場面で新しいパターンが必要になったら、ADR を書いてからこの表に追加します。

| 場面 | パターン | 実装方法 |
| --- | --- | --- |
| OS、デバイス、ネットワークへのアクセス | Ports and Adapters | 4.1 と 5.1 のとおり |
| UI からエンジンへの操作 | コマンドパターン | 1 操作に 1 コマンド（`request_id`、`entity_id`、`expected_revision`）。エンジンが適用して `applied_revision` を返す（`docs/adr/0003-ipc-named-pipe-json.md`） |
| エンジンから UI への通知 | スナップショットとイベント（オブザーバーパターン） | 接続時に全状態のスナップショット、以降は差分イベントを送る |
| 状態遷移（セッション、接続、シグナル） | 遷移表による有限状態機械 | 状態とイベントは `enum class`。遷移は `constexpr` の遷移表（状態 × イベント → 次状態）で 1 か所に定義する。遷移関数は次状態を返す純粋関数とし、副作用は呼び出し側で実行する。状態名は仕様書の正規名（`NO_SIGNAL` など）を使う。GoF の State パターン（状態ごとのクラス階層）は使わない |
| オーディオスレッドへのパラメーター受け渡し | ロックフリー SPSC キュー | フェーダーなどのパラメーターはキューで渡し、5〜20ms のゲインランプで適用する |
| オーディオグラフの変更 | ダブルバッファリング | 新しいグラフをオーディオスレッド外で構築し、`std::atomic` のポインター交換でスワップする。古いグラフはオーディオスレッド外で破棄する |
| リソース管理 | RAII | ファイル、ハンドル、ロック、COM、ソケットは RAII ラッパーで保持する |
| アダプターの生成 | ファクトリー関数 | `make_xxx()` が `std::unique_ptr<ポート>` を返す。呼び出し側は具象クラスに依存しない |
| エラーの返却 | Result 型 | 6.2 のとおり |

### 6.2 エラー処理

- 失敗しうる処理は、モジュールの公開 API で Result 型（P1 で `modules/common` に追加）を返す。例外をモジュール境界の外に送出しない。
- エラーには原因別のエラーコード（仕様書のエラーコード、`REJECTED` など）を付ける。UI はエラーコードからローカライズ済みのメッセージを表示する。
- 処理方法が定まらないエラーは、1.2 のフェイルセーフに従い、該当するチャンネルや出力をミュートする。

### 6.3 外部入力と認可

- ネットワーク、IPC、ファイルからの入力は、信頼境界（`wire/`、`control-protocol`、設定のローダー）でバリデーションし、検証済みの型に変換する。内側の関数は検証済みの型だけを受け取るので、同じバリデーションを重複して書かずに済む。
- 認可はエンジンとランデブーサーバーで行う。UI で要素を非表示にするだけでは認可にならない。
- パケットのパーサーなど外部入力を処理するコードには、P2 以降でファジングを導入する。

### 6.4 ロギングと秘密情報

- ログは `common` の `Logger` ポート経由で出力する。実装は `persistence` が持つ。`std::cout` は `--version` などの CLI 出力だけに使う。
- ログに PCM データ、招待トークン、鍵、個人情報を出力しない。
- 秘密情報は DPAPI か Windows 資格情報マネージャーで保護する（`persistence`）。リポジトリ、設定ファイル、ログに平文で置かない。
- 暗号プリミティブを自作せず、Noise の実装と libsodium を使う（`docs/adr/0001-key-exchange-noise-ik.md`）。

## 7. 新機能の設計手順

1. **関連する仕様と ADR を読む。**
2. **機能を関心事ごとに分解する。** 「外部 I/O」「外部入力のパース」「状態と状態遷移」「ドメインロジック」「UI への通知」「描画」の 6 つに分ける。
3. **関心事ごとに配置を決める。** 5.2 の手順で判定し、3.3 の責務表のモジュールに割り当てる。1 つの機能が複数のモジュールにまたがるのは通常のことで、各部分を責務の合うモジュールに置く。
4. **スレッドを割り当てる。** 2.3 のスレッドモデルで、各部分がどのスレッドで実行されるかを決める。オーディオスレッドで実行する部分は第 9 節のリアルタイム制約に従う。
5. **依存を確認する。** 新しく必要な依存が 4.2 の許可リストにあるか確認する。なければ 4.1 の基準でポートかリスナーを定義し、依存の向きを合わせる。
6. **既存モジュールに収まらない場合だけ、新しいモジュールを作る。** 条件は次の 3 つをすべて満たすこと。
    - 3.3 の責務表のどのモジュールにも該当しない
    - 単独で単体テストできる
    - 4.2 の許可リストに、循環依存を作らずに配置できる

    新しいモジュールは、ADR を書き、2.1、3.3、4.2 の表を更新する PR を先に出してから、5.3 の構成で作る。
7. **内側のレイヤーから実装する。** ポート → ドメインと単体テスト（フェイクを使う） → アダプター → コンポジションルートでの注入 → `control-protocol` のコマンドとイベント → UI（`ipc/` → `state/` → `screens/`）。内側から実装すると、OS や UI なしで単体テストできる状態を保ったまま進められる。
8. **PR はレイヤー単位で分割してよい。** UI まで接続されていない機能の UI 要素は、接続されるまで「開発中」のトースト通知だけにする。

例として、「子機ごとのパケットロス率をミキサー画面に表示する」機能は次のように配置します。

| 関心事 | 内容 | 配置 | スレッド |
| --- | --- | --- | --- |
| 外部入力のパース | パケットのシーケンス番号を読む | 既存の `transport/src/wire/`（変更なし） | ネットワーク受信 |
| ドメインロジック | シーケンス番号の欠落からロス率を算出する | `transport/src/loss_meter.{h,cpp}`、`transport/tests/loss_meter_test.cpp` | ネットワーク受信 |
| 状態 | 参加者ごとのロス率を保持する | `session` の参加者の状態 | コントロール |
| 内側から外側への通知 | ロス率の更新を `session` に通知する | `transport/include/vocroz/transport/ports/stats_listener.h` を `session` が実装する | ネットワーク受信からコントロールへ |
| UI への通知 | ロス率の変化をイベントで送る | `control-protocol` に `ParticipantStatsEvent` を追加 | コントロール |
| 依存性の注入 | 上記を接続する | `apps/pa-engine/src/composition_root.cpp` | |
| 描画 | チャンネルストリップの横に表示する | `ui/src/ipc/`（メッセージ型）、`ui/src/state/`（保持）、`ui/src/screens/mixer/`（描画）、`ui/src/i18n/`（文字列） | |

## 8. コーディング規約

### 8.1 共通

- 識別子は英語、コメントとドキュメントは日本語。
- 文字コードは UTF-8、改行コードは LF（`.editorconfig` に従う）。
- フォーマットはフォーマッター（C++: clang-format、TypeScript: Prettier、Go: gofmt）に任せる。
    - pre-commit フック（`.githooks/pre-commit`）が、ステージされたファイルをコミット前に自動でフォーマットする。
    - フックは CMake の configure 時に `core.hooksPath` の設定で自動的に有効になる。CMake を使わない場合は `git config core.hooksPath .githooks` を 1 回実行する。
    - フォーマッターがない環境では、フックは警告だけを出してコミットを通す。CI ではフォーマットを検査しない。
- コンパイラの警告は修正する（CI は失敗させない）。警告を抑制するときは、その行に理由をコメントで書く。
- マジックナンバーは名前付き定数にし、値の根拠をコメントに書く。

### 8.2 C++（C++20）

**命名規則**（`.clang-tidy` の readability-identifier-naming で検査する）

| 対象 | 規則 | 例 |
| --- | --- | --- |
| 名前空間 | `vocroz::<モジュール名>`、lower_case | `vocroz::audio_core` |
| クラス、構造体、列挙型、型エイリアス | PascalCase | `JitterBuffer` |
| 関数、変数、引数 | snake_case | `push_frame()`、`sample_rate` |
| private データメンバー | snake_case + 末尾 `_` | `target_delay_` |
| 定数（`constexpr`）、列挙子 | `k` + PascalCase | `kSampleRate`、`State::kIdle` |
| マクロ | 使わない（必要な場合は `VOCROZ_` + UPPER_CASE） | |
| ファイル | snake_case | `jitter_buffer.h` |

**記述規則**

- ヘッダーは `#pragma once`。ヘッダーで `using namespace` を書かない。
- 所有権は `std::unique_ptr` で表す。共有所有が必要な場合だけ `std::shared_ptr`。生の `new` と `delete` は使わない。
- 所有権を持たない引数は `std::span`、`std::string_view`、`const&` で受け取る。
- 変数とメンバーは可能な限り `const` にする。値セマンティクスを優先し、共有される可変状態を最小化する。
- 戻り値を無視してはならない関数には `[[nodiscard]]` を付ける。
- 列挙型は `enum class`。`switch` では全列挙子を列挙し、`default` を書かない（列挙子の追加時に `-Wswitch` で漏れを検出するため）。
- キャストは `static_cast` などの名前付きキャストだけを使う。C スタイルキャストは使わない。

### 8.3 TypeScript（UI）

- `strict: true`。`any` は使わない。
- UI の文字列はすべて文字列リソース経由にする（第 12 節）。

### 8.4 Go（ランデブーサーバー）

- `gofmt` と `go vet` を通す。エラーは `fmt.Errorf("...: %w", err)` でラップして返す。
- 外部パッケージは最小限にし、追加するときは PR の概要に理由を書く。

## 9. リアルタイムオーディオ処理の設計

オーディオスレッドはバッファ周期ごとのデッドラインまでに処理を終えなければ、バッファアンダーランでドロップアウトが起きます。そのため、オーディオスレッドで実行する処理は次の方針で設計します。

- **メモリは事前確保する。** バッファとオーディオグラフはオーディオスレッド外で確保し、オーディオスレッドでは使うだけにする。
- **パラメーターは SPSC キューで受け取る。** 他スレッドからの値はロックフリー SPSC キューから取り出す（6.1）。
- **構造の変更はスワップで受け取る。** オーディオグラフの変更は、スレッド外で構築したグラフとのアトミックなポインター交換で反映する（6.1）。
- **エラーは戻り値で返す。** 例外を使わず、エラー時はそのブロックを無音にする。

オーディオスレッドと、そこから呼ばれる関数では、次の操作を使いません。どれも実行時間に上限がない（unbounded）ためです。

- ヒープの確保と解放（`new`、`malloc`、`std::vector` の再確保、`std::string` の生成など）
- ブロッキング同期（mutex、条件変数）、ファイル I/O、ソケット I/O、ロギング、例外の送出
- OS の待機関数（`WaitForSingleObject` など）、プラグインのスキャン

オーディオスレッドから呼ぶ関数は `noexcept` にし、公開ヘッダーのコメントに「オーディオスレッドから呼び出し可能」と明記します。
あわせて、宣言と定義の `noexcept` の後に `VOCROZ_NONBLOCKING`（`vocroz/audio_core/realtime.h`）を付けます。
`audio-core` は Linux の CI（`Linux (RealtimeSanitizer)` のジョブ）で Clang の RealtimeSanitizer（`-fsanitize=realtime`）を有効にしてテストを実行し、`VOCROZ_NONBLOCKING` を付けた関数の実行中の違反を検出します。

## 10. コメント

- **すべてのソースファイル**の先頭に、`SPDX-License-Identifier: AGPL-3.0-only` とファイルの責務の説明を書く。
- **公開ヘッダーのすべての宣言**（クラス、関数、定数）に `///` の Doxygen コメントを書く。関数には引数（`@param`）、戻り値（`@return`）、エラー時の振る舞い、スレッド安全性（どのスレッドから呼び出し可能か）を書く。
- 実装内のコメントには、処理内容（what）ではなく理由（why）を書く。
- 仕様や ADR に由来する処理にも理由を書く。仕様書の記述をそのまま転記しても、要約してもよい。
- **コメントに仕様書の節番号（「仕様 5.5.4」など）を書かない。** 改版で番号がずれたり節が削除されたりすると、参照先を追跡できなくなるためです。コメント単体で理由が理解できるように書く。
    - 良い例: `// RESUMING 中に 1 秒間パケットが届かなければ、回線は復旧していないとみなして NO_SIGNAL に遷移する`
    - 悪い例: `// 仕様 5.5.4 参照`
- ADR はリポジトリ内に残るので、ファイル名で参照してよい（例: `docs/adr/0003-ipc-named-pipe-json.md`）。その場合も理由の要約を書く。
- 古くなったコメントは、コードと同じ PR で修正する。コメントアウトしたコードは残さない（履歴は Git にある）。

## 11. 共通化の基準（DRY）

- **知識の重複は排除する。** 知識とは、同じ規則やドメイン知識（招待コードのフォーマット、dB とリニアゲインの変換など）のこと。2 か所目に書く時点で関数に抽出する。
- **偶発的な重複（accidental duplication）は抽出しない。** コードの形が同じでも、変更理由が異なるなら別の知識として扱う。判定基準は「一方を変更したとき、もう一方も必ず同じ変更が必要か」。必要がなければそれぞれに書いたままにする。偶発的な重複を共通化すると、各呼び出し元の都合で共通関数に引数や分岐が増え、結合度が上がって可読性と変更容易性が下がる。
- 抽出先は 5.2 の 4 と 5 で決める（複数モジュールなら `modules/common`、単一モジュール内なら `src/detail/`）。
- 抽出したユーティリティには単体テストを付ける。

## 12. UI の設計

- UI は状態を導出しない。エンジンから届いたスナップショットとイベントを `state/` のストアに保持して描画し、ユーザー操作は `ipc/` からコマンドとして送る。
- 表示は ACK を受け取ってから更新する。フェーダーなど連続値のドラッグ中だけは楽観的更新を許可する。
- メーターなど高頻度の描画は Canvas で行い、DOM を毎フレーム更新しない。
- モーダルダイアログは不可逆操作の確認だけに使う。エラーや参加申請は非モーダルのトースト通知で出す。
- 未実装機能の UI 要素は「開発中」のトースト通知だけを出す。
- 文字列はハードコードせず、文字列リソースのキーで参照する。日本語は必須、英語は対応できる範囲で用意する。初期ロケールは OS の表示言語に合わせる。
- 新しいキーは日本語と英語の両方のリソースに同時に追加する。英語が未訳なら日本語と同じ文を仮に入れ、未訳であることをコメントで残す。
- 状態の正規名（`NO_SIGNAL` など）は UI に表示せず、表示用ラベルを文字列リソースで定義する。

## 13. テスト

- 単体テストは GoogleTest。対象と同じモジュールの `tests/` に置き、`vocroz_add_unit_test()` で登録する。
- テスト名は `<対象>Test.<条件と期待結果>`（例: `JitterBufferTest.DropsLateFramesAfterReset`）。名前とコメントで検証内容が特定できるようにする。仕様書の受け入れテストとの対応は Issue とマイルストーンで管理し、コードには書かない（第 10 節と同じ理由）。
- 時刻、乱数、ネットワーク、デバイスはポートのフェイク（`tests/fakes/`）に差し替え、テストを決定的にする。

## 14. 開発プロセス

1. 着手前に、関連する仕様と ADR を読む。新機能は第 7 節の手順で設計する。
2. 1 つの PR は 1 つの目的に限る。機能追加とリファクタリングを同じ PR に混ぜない。
3. **既存の振る舞いを変えない。** リファクタリングは、先に現在の振る舞いを固定する特性テスト（characterization test）を書いてから行う。振る舞いを変える必要がある場合は、PR の概要に理由と影響範囲を書く。
4. 新しい設計判断（ライブラリの採用、プロトコルの変更、このファイルの規則の例外など）は、ADR を追加してから実装する。
5. Definition of Done:
    - CI（ビルドとテスト）が成功している
    - 追加、変更した処理に単体テストがある
    - 新しいコードが第 3 節から第 5 節の責務、依存、配置に準拠している
    - 必要なドキュメント（ADR、README、モジュールの README、このファイルの表）が更新されている
6. 静的解析の指摘や既存コードの細かなドリフトは、PR ごとにはブロックしない。第 15 節の定期リファクタリングでまとめて修正する。PR では新しいコードを地図どおりに配置することを守る。

## 15. 定期リファクタリングとコードレビュー

### 15.1 方針

CI はビルドとテストの成否だけを検査します。構造のドリフトは日々の PR ではブロックせず、月次の定期リファクタリングで、現在の位置から第 5 節の地図が示す位置へ移動して修正します。
地図が明確であれば、どのドリフトも移動（Move）、分割（Split）、抽出（Extract）、名前の変更（Rename）の小さなリファクタリングで修正できます。そのため、地図の曖昧さを残さないことを最優先にします。

### 15.2 サイクルの手順

毎月 1 日に「定期リファクタリング」の Issue が自動で作成されます（`.github/workflows/refactor-cycle.yml`）。

1. 静的解析（clang-tidy、go vet）を実行して修正する。フォーマットは pre-commit フックで済んでいるが、フックを経由せずにコミットされたファイルがあれば clang-format と gofmt もここで一括適用する。これを 1 つ目の PR にする。
2. コードベース全体を第 3 節から第 5 節と照合し、ドリフトを Issue の「ずれ一覧」に列挙する（形式は 15.3）。
3. ドリフトを「1 PR に 1 種類のリファクタリング」にまとめて修正する（例: 「定数の配置をそろえる」「main.cpp の条件分岐をモジュールに移す」）。
4. 地図に欠落や矛盾が見つかったら、コードより先にこのファイルを修正する PR を出す。
5. すべて解消したら Issue をクローズする。

### 15.3 ドリフトの記述形式

ドリフトは次の 5 項目で記述します。5 項目がそろっていれば、誰が修正しても同じ結果になります。

| 項目 | 内容 | 例 |
| --- | --- | --- |
| 現在の位置 | ファイルと関数またはクラス | `apps/pa-engine/src/main.cpp` の招待コードのバリデーション |
| あるべき位置と形式 | 第 5 節の表に当てはめた結果 | `modules/session/src/invite_validator.{h,cpp}` の `InviteValidator` |
| 根拠 | このファイルの節番号 | 5.1「ドメインロジック」行、5.1「エントリポイント」行 |
| 修正方法 | Move、Split、Extract、Rename、ポートの導入のいずれか | Move し、main.cpp からは呼び出すだけにする |
| 影響範囲 | 変更するファイルと、振る舞いを固定するテスト | `session` に単体テストを追加してから Move する |

### 15.4 重大度

| 重大度 | 対象 | 扱い |
| --- | --- | --- |
| 即時修正（`status: must-fix`） | 製品要件（1.1）、依存の向き（4.1、4.2）、リアルタイム制約（第 9 節）、外部入力と秘密情報の扱い（6.3、6.4）への違反 | その PR で差し戻す。マージ済みなら定期サイクルを待たずに修正する |
| 次回サイクルで修正（`type: drift`） | 配置、形式、命名、知識の重複、コメント、フォーマット | PR はマージしてよい。ドリフトを Issue（「配置のずれ」テンプレート）に記録する |

### 15.5 レビューの規則

- 指摘には、このファイルの節番号を根拠として書く。根拠を示せない好みは指摘にしない。
- このファイルに書かれていないことでは差し戻さない。必要ならこのファイルの改訂を提案する。
- 次回サイクルで修正するドリフトは、PR をブロックせずに Issue に記録する。
- リファクタリングの PR は、既存のテストを変更せずに成功することを確認する。テストを変更した場合は、振る舞いが変わらない根拠を PR に書く。

## 16. GitHub の運用

- **Issue**: すべての作業は Issue から始める。テンプレートは「機能」「不具合」「配置のずれ」の 3 種類。
- **ラベル**: 種類（`type:`）、領域（`area:`）、状態（`status:`）の 3 軸で付ける。定義は `.github/labels.yml`。
- **マイルストーン**: 実装計画のフェーズ（P0〜P6、Beta1〜6、RC1、v1.0）。定義は `.github/milestones.yml`。ラベルとマイルストーンを変更したら、Actions の「Repository setup」を手動実行して反映する。
- **ブランチ名**: `<種類>/<短い説明>`（例: `feat/jitter-buffer`、`fix/reconnect-strip`、`refactor/move-constants`）。
- **コミットメッセージ**: Conventional Commits（`feat:`、`fix:`、`refactor:`、`docs:`、`test:`、`build:`、`ci:`）。本文は日本語でよい。
- **PR**: テンプレートを埋め、`Closes #<Issue 番号>` で Issue をリンクする。CI の成功後にマージする。`main` へ直接 push しない。
- **GitHub Actions**:
    - `CI`: ビルドとテスト（Windows、Linux、Go）
    - `Refactor cycle`: 月次の定期リファクタリングの Issue を作成する
    - `Repository setup`: ラベルとマイルストーンを同期する（手動実行）
    - `Claude`: Issue や PR のコメントで `@claude` とメンションすると、Claude Code がこのファイルに従って作業する（オーナーとコラボレーターのコメントだけに反応する）
    - Dependabot: GitHub Actions と Go の依存を月次で更新する
- **@claude の使用例**:
    - Issue で `@claude この Issue を AGENTS.md 第 7 節の手順で設計して、実装の PR を出して`
    - PR で `@claude AGENTS.md 第 15.5 節の規則でレビューして`
    - 定期リファクタリングの Issue で `@claude 第 15 節の手順で照合して、ずれ一覧を作って`

## 17. よく使うコマンド

```sh
# C++ のビルドとテスト
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure

# pre-commit フックを有効にする（CMake の configure 時に自動で有効になる）
git config core.hooksPath .githooks

# 全体のフォーマットと静的解析
git ls-files '*.cpp' '*.h' | xargs clang-format -i
run-clang-tidy -p build -quiet '(apps|modules)/.*/src/'

# ランデブーサーバー
cd infra/rendezvous && gofmt -l . && go vet ./... && go test ./...
```

GoogleTest を取得できないオフライン環境では、`-DVOCROZ_BUILD_TESTS=OFF` で単体テスト以外をビルドできる。
