# 0011. UI のフロントエンドは Vite + Preact + TypeScript で作り、フェイクエンジンで単独に開発する

- 状態: 採用（2026-11-07）

## コンテキスト

`docs/adr/0005-ui-juce-webview.md` で、画面は `ui/` の TypeScript のフロントエンドを JUCE の `WebBrowserComponent` で表示すると決めた。
フロントエンドのライブラリ、ビルドとテストの道具、エンジンなしで画面を開発する方法は決まっていなかった。
エンジン（`pa-engine`、子機ロジック）と `control-protocol` は並行して作るので、UI はそれらを待たずに作り始め、ブラウザだけで動かせる必要がある。
また、フェーダーの楽観的更新の細部（送信の間引き、ACK の期限）も UI 側で決める必要があった。

## 決定

### ライブラリと道具

| 用途 | 採用 | ライセンス |
| --- | --- | --- |
| 画面の描画 | Preact 10 | MIT |
| ビルドと開発サーバー | Vite 7 | MIT |
| 型 | TypeScript 5（`strict: true`） | Apache-2.0 |
| 単体テスト | Vitest 3、jsdom、@testing-library/preact | MIT |
| 整形 | Prettier 3 | MIT |

- 実行時に読み込むのは Preact だけにする。ほかはすべて開発時の依存（`devDependencies`）で、成果物には含まれない。
- JSX は Vite（esbuild）の設定で Preact に変換し、Babel のプラグインに依存しない。
- バージョンは `package.json` で固定し、`package-lock.json` をコミットする。CI は `npm ci` で再現する。
- 状態管理のライブラリは使わず、`ui/src/state/` の小さなストア（値の保持と購読だけ）で足りる範囲にとどめる。

### エンジンなしでの開発

- `ui/src/ipc/transport.ts` の `Transport` を、本番では JUCE のネイティブイベント（`juce-transport.ts`）、開発とテストではフェイクエンジン（`ui/src/ipc/fake-engine/`）が実装する。
- ページが JUCE の外で開かれたとき（`window.__JUCE__` がないとき）だけ、フェイクエンジンを動的に読み込む。モード、表示言語、招待は JUCE の初期化データか、同じ名前の URL のクエリ（`?mode=client&lang=en&join=...`）で受け取る。
- `control-protocol` が入るまでは、`ui/src/ipc/messages.ts` を仮のスキーマとして使い、入ったらその定義に合わせて置き換える。

### 操作と表示の規則

- 離散操作（ミュート、承認など）は `request_id`、`entity_id`、`expected_revision` を付けて送り、表示はエンジンから届いた状態で変える。ACK を待つ間はボタンに待機中の表示を出し、同じ entity への重ねての送信はしない。2 秒応答がなければ失敗として通知する。
- 連続値（フェーダー、送信レベル）は entity ごとに約 60 Hz（16 ms 間隔）に間引いて送り、操作の終了時に残りをすぐ送る。最も古い未 ACK の送信から 500 ms 以内に ACK が来なければ、手元の表示をエンジンの値に戻して警告する。
- 連続値の変更では revision を進めない（seq の後勝ちで扱う）。フェーダーの操作中に同じ entity のミュートを押しても `REVISION_CONFLICT` にならないようにするため。この扱いは `control-protocol` の設計に引き継ぐ。
- メーターは `ui/src/state/meter-store.ts` に保持し、Canvas が描画のたびに読む。画面全体の再描画は起こさない。

## 理由

- Preact は React と同じ書き方で、実行時のサイズが小さい。WebView2 は Chromium なので、互換のための変換も要らない。
- Vite と Vitest は設定を共有でき、テストの実行も速い。テストを jsdom で動かせば、Windows も WebView2 もない Linux の CI で UI を検証できる。
- フェイクエンジンを `Transport` の実装にすれば、画面のコードは本番と同じ経路でコマンドと ACK をやり取りする。テストでも同じフェイクを使い、時刻はフェイクのタイマーで進めるので結果が決定的になる。
- 連続値の変更で revision を進めると、ドラッグの直後のミュートが毎回衝突し、操作者が押し直す必要が出る。

## 結果

- CI に UI のジョブ（`npm ci`、型検査、テスト、ビルド）を追加する。
- AGENTS.md の 4.2（外部ライブラリの使用範囲）、4.5（UI 内の依存）、5.1（配置表）に、UI の共通部分とフェイクエンジンの置き場所を追記する。
- `modules/webui` は、イベント ID `vocroz.to_engine`（UI からネイティブ側）と `vocroz.from_engine`（ネイティブ側から UI）で JSON の文字列を中継し、初期化データで `mode`、`lang`、`join` を渡す。
