# ランデブーサーバー

親機と子機が直接つながるための仲介をする Go の単一バイナリです。
音声は通さず、メディア鍵も持ちません。サーバーが止まっても、確立済みのセッションは続きます。
設計判断は [docs/adr/0009-rendezvous-protocol.md](../../docs/adr/0009-rendezvous-protocol.md) にあります。

| 機能 | ポート | 内容 |
| --- | --- | --- |
| STUN Binding | UDP（既定 3478） | RFC 8489。送信元の外部アドレス（サーバー再帰アドレス）を返す |
| ランデブーメッセージ | UDP（STUN と同じポート） | 候補アドレスの交換と、ホールパンチ開始の同期 |
| 招待 API | HTTP(S)（既定 8080） | 招待の登録、解決、失効 |
| `/join` | HTTP(S) | 招待 URL（`https://<サーバー>/join#<招待コード>`）の着地ページ |
| `/healthz` | HTTP(S) | 死活監視。`ok <バージョン>` を返す |

## 起動

```sh
cd infra/rendezvous
go build ./cmd/rendezvous
./rendezvous -http :8080 -udp :3478
```

| 引数 | 既定値 | 内容 |
| --- | --- | --- |
| `-http` | `:8080` | HTTP(S) の待ち受けアドレス |
| `-udp` | `:3478` | STUN とランデブーメッセージの待ち受けアドレス |
| `-tls-cert`、`-tls-key` | なし | 指定すると HTTPS で待ち受ける。前段のリバースプロキシで TLS を終端する場合は指定しない |
| `-download-url` | GitHub の最新リリース | `/join` ページのダウンロードリンク |

状態はメモリにだけ置くので、再起動すると有効な招待は消えます（親機が作り直します）。
UDP を受けられるホスト（VPS や仮想マシン）で動かしてください。UDP を受けられないサーバーレス基盤では HTTP 側しか動きません。

## 招待 API

本文はすべて JSON で、バイト列は base64url（パディングなし）です。
招待コードや鍵を URL に入れないように、解決と失効も POST の本文で渡します。
応答には `Cache-Control: no-store` が付きます。

### `POST /v1/invites`（親機）

```json
{"host_public_key": "<32 バイト>", "invite_token": "<16〜64 バイト>", "rendezvous_id": "<16 バイト>", "ttl_seconds": 1800}
```

`ttl_seconds` は省略できます（省略か 0 なら 1800 秒。1800 秒を超える値は 1800 秒に切り詰めます）。

`201 Created`:

```json
{"code": "GG7PF2XWSC", "display_code": "GG7PF-2XWSC", "expires_at": "2026-11-07T14:10:09Z", "revoke_key": "<32 バイト>"}
```

`revoke_key` は親機だけが保持し、ログに出さないでください。

### `POST /v1/invites/resolve`（子機）

```json
{"code": "gg7pf-2xwsc"}
```

招待コードは大文字小文字、ハイフン、空白、`O` と `0`、`I`・`L` と `1` の違いを無視します。

`200 OK`:

```json
{"code": "GG7PF2XWSC", "host_public_key": "…", "invite_token": "…", "rendezvous_id": "…", "expires_at": "2026-11-07T14:10:09Z"}
```

### `POST /v1/invites/revoke`（親機）

```json
{"code": "GG7PF2XWSC", "revoke_key": "…"}
```

成功すると `204 No Content`。失効済みや期限切れの招待に送っても成功します。

### エラー

本文は `{"error": "<エラーコード>"}` です。

| 状態 | エラーコード | 意味 |
| --- | --- | --- |
| 400 | `INVALID_REQUEST` | 本文の形式やフィールドの長さが正しくない |
| 400 | `INVALID_CODE` | 招待コードの形式が正しくない |
| 403 | `FORBIDDEN` | 失効用の鍵が違う |
| 404 | `INVITE_NOT_FOUND` | 招待がない（期限切れや失効から 24 時間を過ぎたものを含む） |
| 410 | `INVITE_EXPIRED` | 招待の期限が切れている |
| 410 | `INVITE_REVOKED` | 招待が失効している |
| 503 | `CAPACITY_EXCEEDED` | サーバーが保持できる招待の上限に達している |

## ランデブーメッセージ

形式の正本は [internal/udpapi/wire.go](internal/udpapi/wire.go) です。流れは次のとおりです。

1. 親機は招待の登録に使ったランデブー ID で REGISTER（役割は親機）を送り、10 秒程度の間隔で送り直す。送り直しは、サーバーへの NAT のマッピングを保つことも兼ねる。
2. 子機は招待の解決で得たランデブー ID で REGISTER（役割は子機）を送る。接続できるまで送り直す。
3. サーバーは REGISTER ごとに REGISTERED（サーバー再帰アドレスと、相手がいるかどうか）を返す。
4. 親機と子機の両方の登録がそろうと、サーバーは双方に PUNCH（相手のピア ID と候補アドレス）を同時に送る。候補アドレスは相手のサーバー再帰アドレスが先頭で、そのあとに相手が申告したローカルアドレスが続く。
5. PUNCH を受け取った双方は、候補アドレスへ同時に送信を始めて NAT のマッピングを開き、Noise_IK のハンドシェイクに進む。

登録は 30 秒更新がなければ消えます。サーバーが応答するのは正しい形式のパケットだけです。

## パッケージ構成

| パッケージ | 種類 | 責務 |
| --- | --- | --- |
| `cmd/rendezvous` | エントリポイント兼コンポジションルート | 引数の検証、組み立て、起動と終了 |
| `internal/httpapi` | 外部インターフェース | 招待 API、`/join` ページ、`/healthz` |
| `internal/udpapi` | 外部インターフェース | UDP の振り分け、ランデブーメッセージのワイヤーフォーマット |
| `internal/stun` | 外部インターフェース | STUN Binding のパースと応答 |
| `internal/invite` | ドメイン | 招待コード、招待の登録・解決・失効 |
| `internal/signaling` | ドメイン | 親機と子機の引き合わせ（候補アドレスの交換） |

外部インターフェースのパッケージからドメインのパッケージへだけ依存します（AGENTS.md 5.5）。

## テスト

```sh
cd infra/rendezvous && gofmt -l . && go vet ./... && go test ./...
```
