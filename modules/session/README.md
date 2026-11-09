# session

親機と子機のセッションの規則を持つドメインのモジュールです。依存してよいのは `transport`、`control-protocol`、`audio-core`、`common` で（AGENTS.md 4.2）、今は `control-protocol`、`audio-core`（プラグインの処理の型）、`common` を使っています。
分担、制御メッセージの形式、再接続トークンと招待の扱いは `docs/adr/0022-session-domain.md` で決めています。

## 責務

- 状態機械: `SessionState`、`ConnectionState`、`OutputDeviceState`、`ClientState`（`src/state/` の遷移表）
- 招待コード、招待 URL、`vocroz://` の引数のパースと検証。招待の発行、期限切れでの発行し直し、失効、申請の照合
- 承認と拒否、再接続トークンによる同じストリップへの復帰、全員 `LOST` での `READY` への復帰
- 参加者とチャンネルストリップの状態、コマンドの適用（revision の競合の拒否、重複した `request_id` の無視）と認可
- 初期ミュート、出力デバイスの抜去時の Master の `SAFETY` ミュート、子機の「入力なし」の検知
- 子機と親機の間の制御メッセージのエンコードとデコード（信頼境界、`src/wire/`）
- プラグインの一覧: スキャンの記録のキャッシュ、読み込み不可のファイルの記録、スキャンの進行（`PluginCatalog`、`docs/adr/0032-plugin-catalog-and-loader.md`）

ワイヤーフォーマットの外枠と暗号化（`transport`）、DSP（`audio-core`）、ソケットと HTTP（`network`）、設定ファイルの形式（`persistence`）は持ちません。

## 公開 API

| ヘッダー | 内容 |
| --- | --- |
| `vocroz/session/host_session.h` | `HostSession`（親機。`pa-engine` が使う） |
| `vocroz/session/client_session.h` | `ClientSession`（子機。`remote-pa` の子機モードが使う） |
| `vocroz/session/invite_code.h` | `InviteCode`、`parse_invite()`、`parse_invite_code()`、`make_invite_url()` |
| `vocroz/session/identifiers.h` | `HostPublicKey`、`InviteToken`、`ReconnectToken`、`RendezvousId`、`RevokeKey`、`PeerHandle` |
| `vocroz/session/settings.h` | `HostSettings`、`ClientSettings`、`SettingsError` |
| `vocroz/session/ports/mixer_control.h` | `MixerControl`（親機のミキサーへの反映） |
| `vocroz/session/ports/peer_link.h` | `PeerLink`（親機から子機との接続の操作） |
| `vocroz/session/ports/host_link.h` | `HostLink`、`HostEndpoint`（子機から親機への接続の操作） |
| `vocroz/session/ports/capture_control.h` | `CaptureControl`（子機のキャプチャと送信への反映） |
| `vocroz/session/ports/rendezvous_client.h` | `RendezvousClient`（招待の登録、解決、失効） |
| `vocroz/session/ports/settings_store.h` | `SettingsStore`（設定の読み込みと保存） |
| `vocroz/session/ports/secure_random.h` | `SecureRandom`（トークンの生成） |
| `vocroz/session/ports/ui_event_sink.h` | `UiEventSink`（UI へのメッセージ） |
| `vocroz/session/plugin_catalog.h` | `PluginCatalog`、`PluginScanProgress`（プラグインの一覧とスキャンの進行） |
| `vocroz/session/plugin_description.h` | `PluginDescription`（スキャンで見つけたプラグインの記述） |
| `vocroz/session/plugin_scan_record.h` | `PluginFile`、`PluginScanRecord`、`PluginScanStatus`、`PluginScanFailure` |
| `vocroz/session/ports/plugin_scanner.h` | `PluginScanner`（プラグインファイルの列挙とスキャン） |
| `vocroz/session/ports/plugin_catalog_store.h` | `PluginCatalogStore`（スキャンの記録の保存） |
| `vocroz/session/ports/plugin_loader.h` | `PluginLoader`、`PluginInstance`（プラグインの読み込みと状態の保存と復元） |

テストでは `vocroz::session_fakes` をリンクすると、`fakes/fake_<役割名>.h` で各ポートのフェイクを使えます。

## スレッド

`HostSession` と `ClientSession` のメンバー関数は、すべてコントロールスレッドから呼びます。オーディオスレッドからは呼びません。
ネットワーク受信スレッド、デバイス、ランデブーサーバーとの通信の結果は、コンポジションルートがコントロールスレッドへ移してから渡します。
`tick()` は 100 ms〜1 s の間隔で呼び、招待の期限と再接続の猶予（120 秒）を進めます。

## 組み立ての例（親機）

```cpp
// 受信: UI のメッセージはデコードしてから渡す。不正なものは渡さない。
if (auto message = control_protocol::decode_ui_to_engine(text)) {
    host_session_.handle_ui_message(message.value());
}
// 子機の制御メッセージは、信頼制御層から取り出した本文をそのまま渡す（検証は session が行う）。
host_session_.on_peer_message(peer, body);
```
