# 0014. UI とエンジン間のメッセージの形、revision の進め方、検証の範囲

- 状態: 採用（2026-11-08）

## コンテキスト

`docs/adr/0003-ipc-named-pipe-json.md` で、UI とエンジンの間は長さ付きの JSON でやり取りし、離散操作は `request_id`、`entity_id`、`expected_revision` を持ち、連続値は entity ごとのシーケンス番号で後勝ちにすると決めた。
一方で、次の点は決まっていなかった。

- メッセージの JSON の形（種別の表し方、-∞ dB の表し方、エラーコードの一覧）。
- revision をどの変更で進めるか。`docs/adr/0011-ui-frontend-stack.md` は「連続値の変更では revision を進めない」と決めたが、接続状態や信号状態のようにエンジンが観測して変わる値の扱いは決めていない。観測値の変化でも revision を進めると、信号が途切れたり戻ったりしている最中のミュートや、子機の接続処理中の送信ミュートが `REVISION_CONFLICT` で拒否される。
- 信頼境界（`control-protocol`）でどこまで検証するか。
- ADR-0003 の「C++ と TypeScript の型を生成する」をどう実現するか。

## 決定

### メッセージの形

- 1 つのメッセージは 1 つの JSON オブジェクトで、`type` に種別を小文字の snake_case で書く（例: `set_channel_mute`、`channel_updated`）。フィールド名も snake_case にする。
- 状態は仕様書の正規名（`UPPER_SNAKE_CASE`）の文字列、フラグは真偽値で送る。
- ゲイン（dB）は数値で、-∞ dB は `null` で表す。JSON は無限大を表せないため。
- 離散コマンドは `request_id`、`entity_id`、`expected_revision` を持つ。エンジンは適用したら `ack`（`request_id`、`applied_revision`）、拒否したら `reject`（`request_id`、`error_code`）を返す。
- 連続値のコマンドは `set_gain`（`entity_id`、`seq`、`gain_db`）の 1 種類にする。エンジンは適用したら `gain_ack`（`entity_id`、`seq`、`applied_revision`）を返す。`gain_ack` の `seq` は、その値以下のコマンドがすべて適用済みであることを表す。
- UI は接続したら `hello` を送り、エンジンはそのモードのスナップショット（`mixer_snapshot` か `client_snapshot`）を返す。以降は差分イベント（`channel_updated` など）で知らせる。UI が再起動したときも同じ手順で全状態を受け取り直す。
- メーターは `meters`（entity ごとのプリフェーダーのピーク。dBFS、`null` は無音）で、エンジンが 10〜30 Hz で送る。
- エラーコードは `control-protocol` で 1 か所に列挙する。子機の `FAILED` の原因、セッションの `ERROR` の原因、コマンドの拒否理由に同じ一覧を使う。

### revision の進め方

entity ごとの revision は、**コマンドで変更できる値**（ミュート、入力の選択、承認待ちの有無など）が変わったときだけ進める。

- エンジンがコマンドで変更できる値を自分で変えた場合（出力デバイスの消失による Master の `SAFETY` ミュートなど）も revision を進める。古い表示に基づく操作（ミュートされたことを知らないまま押した解除）を `REVISION_CONFLICT` で拒否するため。
- エンジンが観測して変わるだけの値（`connection_state`、`signal_state`、`client_state`、`input_missing`、`output_device_state`、子機から通知された `send_muted` など）の変化では revision を進めない。差分イベントは送るが、`revision` は変わらない。
- 連続値（`set_gain`）では revision を進めない（ADR-0011 のとおり）。
- 状態によって適用できないコマンド（`CONNECTED` 中の `join` など）は、revision ではなく状態で判断し、`INVALID_STATE` で拒否する。

子機の状態を接続と送信の 2 つの entity に分ける案もあったが、上の規則で衝突はなくなるので、entity は 1 つ（`client`）のままにする。

### 信頼境界での検証

UI からエンジンへのメッセージは、`control-protocol` のデコーダーで次のとおり検証し、検証済みの C++ の型だけを内側（`session`）に渡す。

- JSON として読めないもの、オブジェクトでないもの、`kMaxMessageBytes`（64 KiB）を超えるものは拒否する。
- 未知の `type`、必須のフィールドの欠落、型の違うフィールド、**未知のフィールド**は拒否する。UI とエンジンは同じインストーラーで配るので、互換のために未知のフィールドを許す必要はなく、綴りの誤りを早く見つけられる。
- 範囲の検査は、どの entity にも共通する範囲だけを行う。
  - `expected_revision` と `seq` は 0 以上 2^53 − 1 以下の整数（JavaScript で正確に表せる範囲）。
  - `gain_db` は `null` か、有限の −60 dB 以上 +12 dB 以下（ミキサーのフェーダーと子機の送信レベルの範囲を合わせたもの）。
  - `request_id`、`entity_id` は 1〜64 文字の英数字と `_`、`-`、`.`、`:`。`invite` は 1〜2048 バイト、`device_id` は `null` か 1〜512 バイト。
- entity ごとの範囲（Master とチャンネルのフェーダーは +10 dB まで、など）、entity の存在、状態との整合は `session` が判断する。

エンジンから UI へのメッセージのデコーダーも同じ規則で用意する（往復のテストと、将来 C++ 側で受け取る場合のため）。

### C++ と TypeScript の型の対応

ADR-0003 の型の生成は行わず、`control-protocol` の C++ の型を正本とし、`ui/src/ipc/messages.ts` を手で対応させる（AGENTS.md 4.4）。
対応の正しさは、`modules/control-protocol/tests/data/` に置く全メッセージの JSON の例で確かめる。C++ のテストは、この例をデコードして同じ JSON にエンコードし直せることを確かめる。UI のテストは、この例を `messages.ts` の型として読み込んでストアに適用し、種別とフィールド名の食い違いがないことを確かめる。

### MessageChannel と ChannelRelay

- `MessageChannel` ポートは、1 メッセージ分の JSON の文字列を送受信する双方向の経路で、名前付きパイプ（`ipc`）と WebView のブリッジ（`webui`）が実装する。フレーミング（長さの前置き）は実装が受け持つ。
- `ChannelRelay` は 2 つの `MessageChannel` の間で文字列をそのまま中継する。親機モードの `remote-pa` で、WebView と `pa-engine` をつなぐ。中継ではデコードせず、検証は受け取る側のエンジンで 1 回だけ行う。中継の遅れとコピーを最小にするため。

## 理由

- revision を「操作者が変えられる値の版」に限ると、競合の検出が本来の目的（古い表示に基づく操作の拒否）だけに働き、本番中に頻繁に変わる信号状態のせいでミュートが拒否されることがなくなる。ミュートは安全のための操作なので、押したときに確実に効く必要がある。
- 型の生成には、スキーマの記述言語と生成器を新たに導入する必要がある。メッセージの数は 30 に満たず、JSON の例を両側のテストで共有すれば食い違いは CI で見つかるので、生成の仕組みを持つほうが手間が大きい。
- 検証を entity に依存しない範囲に限ると、`control-protocol` はセッションの状態を知らずに済み、責務表（AGENTS.md 3.3）のとおり DTO とシリアライズだけを持てる。

## 結果

- `modules/control-protocol` に、DTO、JSON のエンコーダーとデコーダー、`MessageChannel`、`ChannelRelay` を置く。
- `session` は、上の revision の規則に従って差分イベントを作る。
- UI のフェイクエンジン（`ui/src/ipc/fake-engine/`）も、観測値の変化では revision を進めないようにそろえる。
- 新しいメッセージを足すときは、C++ の型、エンコーダーとデコーダー、`tests/data/` の例、`ui/src/ipc/messages.ts` を同じ PR で変更する。
