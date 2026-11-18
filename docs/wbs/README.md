# WBS

VOCROZ の計画（WBS）の正本は [`wbs.json`](wbs.json) です。
形式は [single-file-wbs](https://github.com/piguo45/single-file-wbs)（WBS Viewer v2.0.0）の `wbs.json` で、これを VOCROZ の WBS の標準の形式にします。

## 開き方

1. Chrome で [WBS Viewer](https://piguo45.github.io/single-file-wbs/wbs_viewer.html) を開く（Releases の `wbs_viewer.html` を手元で開いてもよい）。
2. 「ファイルを開く」で `docs/wbs/wbs.json` を選ぶ。

データはブラウザの中だけで処理され、外部には送られません。
ガント、進捗軸、依存の 3 つのタブで同じデータを見られます。

## 正本の分担

| 何を | 正本 | 中身 |
| --- | --- | --- |
| 計画（いつ、誰が、どれだけ） | `wbs.json` の `tasks` | 予定、実績、依存 |
| 課題（何を、なぜ、どうなったら終わりか） | GitHub Issue | 範囲、完了条件、議論、PR とのリンク |

課題は GitHub Issue のまま管理します。PR の `Closes #N`、マイルストーン、`@claude` の運用が Issue を前提にしているためです。
single-file-wbs の「GitHub Issue を正本にしているリポの互換手順」に沿い、`wbs.json` の `issues` は使いません。同じ課題を 2 か所に持たないためです。

- 作業パッケージ（WP）の葉 1 つが、WP の Issue 1 件に対応します。
- 葉の `_issue` に Issue 番号を、`note` の末尾に Issue の URL を書きます。
- 案件の `_issue` は親 Issue の番号です（Beta5 まで: #4、Beta6: #134）。
- 葉の名前の先頭の `[F1]` や `[V0]` は WP の ID で、Issue のタイトルの先頭と同じです。

## 案件

| 案件 | 中身 | 移行元 |
| --- | --- | --- |
| Beta6（映像・録画） | V0〜V10 と PO の作業。第 1〜4 陣を集計ノードにしている | WBS ドキュメントの「Beta6」タブ |
| Beta5 まで | F1〜B5 の 28 WP と、PO の作業 H1〜H5。波（W0〜W4）を集計ノードにしている | WBS ドキュメントの「Beta5 まで」タブ |
| 製品全体（v1.0 まで） | 成果物ベースの 180 WP（3 階層） | `vocroz-wbs.json`（2026-11-11） |

## 移行の規則

- **予定（`plan`）**: Beta5 は移行元の「波と目安」の日付です。Beta6 は「コードがそろうまで 1〜1.5 週間」の見込みを陣ごとに割った目安で、移行時に置きました。予定日のない WP（B2b、B3、人の作業、製品全体の WP）は `null` です。
- **実績（`actual`）**: `start` はその Issue を `Closes` する最初の PR を作った日、`end` は Issue を閉じた日です。製品全体の WP は、2026-11-11 時点の状態欄に書かれた PR から取りました（「製作済み」で PR がすべてマージ済みなら `end` も入れています）。
- **工数（`qty`、`hours`）**: WP ごとの見積もりはないので、どの葉も `qty: 1`、`hours: 8` にしています。工数の列は WP の数として読み、進捗率は WP の数の割合になります。
- **依存（`_deps`）**: Beta5 と Beta6 の、予定日がある葉にだけ書いています。依存タブで最長経路を見られます。製品全体の依存は `_wbs.deps` に元の文字列のまま残しています（予定日がないため、図には載せていません）。
- **元の項目**: 製品全体の WP の種類、ゲート、成果物、目的、範囲、範囲外、完了の定義、依存、担当、参照、2026-11-11 時点の状態、リスクは `_wbs` にそのまま入れています。

## 更新のしかた

人は WBS Viewer の編集モードで、AI は `wbs.json` を直接編集します。AI は single-file-wbs の [AGENTS.md](https://github.com/piguo45/single-file-wbs/blob/main/AGENTS.md) の作法に従います。

- 着手したら、その葉の `actual.start` に日付を入れる。
- PR がマージされて Issue が閉じたら、`actual.end` に日付を入れる。
- 予定を理由があって動かすときは、`plan` を直して `_planLog` に 1 行足す（リスケ）。入力の誤りを直すだけなら `plan` だけを直す（訂正）。
- 新しい WP は、Issue を作ってから葉を 1 つ足し、`_issue` と `note` の URL を書く。

変更したら、single-file-wbs の検査スクリプトでエラーがないことを確かめます。

```sh
python3 <single-file-wbs のパス>/scripts/check.py docs/wbs/wbs.json
```
