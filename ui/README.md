# ui

JUCE の `WebBrowserComponent`（Windows のバックエンドは WebView2）上で動く TypeScript のフロントエンドです。親機モード（ミキサー画面）と子機モードの両方の画面をここに置き、起動時にネイティブ側から受け取ったモードで画面を選びます。
ビルド成果物（`dist/`）は `remote-pa` にバイナリリソースとして埋め込みます。
構成と依存の規則は AGENTS.md 第 4.5 節と第 5.5 節、背景は `docs/adr/0005-ui-juce-webview.md` と `docs/adr/0011-ui-frontend-stack.md` を参照してください。

## 開発

Node.js 22 以上が必要です。

```sh
cd ui
npm ci
npm run dev        # 開発サーバー（フェイクエンジンにつながる）
npm test           # 単体テスト
npm run build      # 型検査と dist/ への出力
```

ブラウザで開くと、エンジンの代わりにフェイクエンジンにつながります。URL のクエリで起動時の設定を変えられます。

| クエリ | 意味 |
| --- | --- |
| `?mode=client` | 子機の画面を開く（省略すると親機のミキサー画面） |
| `?lang=en` | 表示言語を英語にする（省略するとブラウザの言語） |
| `?join=<招待>` | 子機の画面の招待の欄に入れておく |

ブラウザのコンソールから `window.vocrozFake` で異常系を再現できます。

| 呼び出し | 再現する状況 |
| --- | --- |
| `vocrozFake.simulateOutputLost()` | 親機の出力デバイスの抜去（Master が SAFETY MUTE になる） |
| `vocrozFake.simulateJoinRequest("名前")` | 参加の申請 |
| `vocrozFake.simulateConnection("ch1", "RECONNECTING")` | 参加者の回線の途絶 |
| `vocrozFake.setDropAcks(true)` | エンジンが ACK を返さない（表示が戻って警告が出る） |
| `vocrozFake.simulateInputMissing()` | 子機のマイクの抜去 |
| `vocrozFake.simulateReconnect()` | 子機の回線の途絶と再接続 |

子機の招待に `expired`、`reject`、`unreachable` を含めると、それぞれ招待の期限切れ、参加の拒否、NAT 越えの不成立になります。
