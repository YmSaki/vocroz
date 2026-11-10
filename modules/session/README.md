# session

親機と子機のセッションの規則を持つドメインのモジュールです。依存してよいのは `transport`、`control-protocol`、`audio-core`、`common` で（AGENTS.md 4.2）、今は `transport`（SRTP 外部出力の鍵の型と検証）、`control-protocol`、`audio-core`（ルーティングのループの検出、プラグインの処理の型、子機の MIDI のキュー）、`common` を使っています。
分担、制御メッセージの形式、再接続トークンと招待の扱いは `docs/adr/0022-session-domain.md` で、ルーティングのステージングと適用は `docs/adr/0026-routing-staging.md` で、ルーティングの保存と起動時の復元は `docs/adr/0033-routing-persistence.md` で、子機の MIDI は `docs/adr/0029-midi-and-vsti.md` で、SRTP 外部出力は `docs/adr/0040-srtp-external-output.md` で決めています。

## 責務

- 状態機械: `SessionState`、`ConnectionState`、`OutputDeviceState`、`ClientState`（`src/state/` の遷移表）
- 招待コード、招待 URL、`vocroz://` の引数のパースと検証。招待の発行、期限切れでの発行し直し、失効、申請の照合
- 承認と拒否、再接続トークンによる同じストリップへの復帰、全員 `LOST` での `READY` への復帰
- 子機の入力（ストリーム）の申告と、入力ごとのストリップの対応（1 台の子機は最大 4 本のストリップになる。`docs/adr/0035-client-input-expansion.md`）
- 参加者とチャンネルストリップの状態、コマンドの適用（revision の競合の拒否、重複した `request_id` の無視）と認可
- 初期ミュート、出力デバイスの抜去時の Master の `SAFETY` ミュート、子機の「入力なし」の検知
- 子機と親機の間の制御メッセージのエンコードとデコード（信頼境界、`src/wire/`）
- ルーティングのステージング、検証（参照、組み合わせ、値の範囲、ループ）、ループの承認、適用と、バスとセンドのミュートとフェーダー（`src/routing_book`、`src/routing_validator`）
- ルーティングの保存（ストリップを参照しない部分とフェーダーの値）と、起動時のミュートでの復元（`HostContext::restore_routing()`）
- 返送モニター（Minus-Me）のミュート（`return_muted`）と世代番号、接続の送信側（`PeerLink::set_return_mute()`）と子機（`ReturnMuteNotice`）への通知、再接続での送り直し（`docs/adr/0036-return-monitor.md`）
- 子機の MIDI: MIDI レーンのメッセージのデコード（`src/wire/midi_message_codec`）、子機の時刻から親機の時刻への写し方（`src/midi_timeline`）、承認済みの接続だけを受け付けてストリップごとのキューに積む受け口と、途絶、切断、ストリップの削除での Panic（`PeerMidiInbox`）
- SRTP 外部出力: 設定（宛先、送り出すバス、プロファイル、ペイロードタイプ、暗号スイート）と鍵の検証と保存、開始と停止と接続テストの状態（`src/external_output_book`）。起動時は停止から始め、鍵と宛先がなければ始めない。送り出すバスがルーティングの適用でなくなったら止める
- プラグインの一覧: スキャンの記録のキャッシュ、読み込み不可のファイルの記録、スキャンの進行（`PluginCatalog`、`docs/adr/0032-plugin-catalog-and-loader.md`）

ワイヤーフォーマットの外枠と暗号化（`transport`）、DSP（`audio-core`）、ソケットと HTTP（`network`）、設定ファイルの形式（`persistence`）は持ちません。

## 公開 API

| ヘッダー | 内容 |
| --- | --- |
| `vocroz/session/host_session.h` | `HostSession`（親機。`pa-engine` が使う） |
| `vocroz/session/client_session.h` | `ClientSession`（子機。`remote-pa` の子機モードが使う） |
| `vocroz/session/invite_code.h` | `InviteCode`、`parse_invite()`、`parse_invite_code()`、`make_invite_url()` |
| `vocroz/session/identifiers.h` | `HostPublicKey`、`InviteToken`、`ReconnectToken`、`RendezvousId`、`RevokeKey`、`PeerHandle` |
| `vocroz/session/routing_delay.h` | `delay_samples()`（ルーティングの遅延の ms からサンプル数への換算） |
| `vocroz/session/settings.h` | `HostSettings`、`ClientSettings`、`SettingsError` |
| `vocroz/session/ports/mixer_control.h` | `MixerControl`（親機のミキサーへの反映。ルーティングの適用を含む） |
| `vocroz/session/ports/output_device_control.h` | `OutputDeviceControl`（出力デバイスの一覧、開く、閉じる。親機は Master の出力、子機は返送モニターの出力） |
| `vocroz/session/ports/peer_link.h` | `PeerLink`（親機から子機との接続の操作。返送のミュートの反映を含む） |
| `vocroz/session/ports/host_link.h` | `HostLink`、`HostEndpoint`（子機から親機への接続の操作。返送のミュートの受信側への反映を含む） |
| `vocroz/session/ports/capture_control.h` | `CaptureControl`（子機のキャプチャと送信への反映） |
| `vocroz/session/ports/rendezvous_client.h` | `RendezvousClient`（招待の登録、解決、失効） |
| `vocroz/session/ports/secret_store.h` | `SecretStore`、`SecretSlot`、`SecretError`（親機と子機の静的秘密鍵と、SRTP 外部出力の鍵の保存。`persistence` が DPAPI で実装する） |
| `vocroz/session/ports/settings_store.h` | `SettingsStore`（設定の読み込みと保存） |
| `vocroz/session/ports/secure_random.h` | `SecureRandom`（トークンの生成） |
| `vocroz/session/ports/ui_event_sink.h` | `UiEventSink`（UI へのメッセージ） |
| `vocroz/session/ports/playout_control.h` | `PlayoutControl`（全チャンネルの目標遅延の反映。`pa-engine` が受信ストリームに配る） |
| `vocroz/session/ports/external_output_control.h` | `ExternalOutputControl`、`ExternalOutputPlan`（SRTP 外部出力の開始、停止、接続テスト。`pa-engine` が送信スレッドを組み立てる） |
| `vocroz/session/peer_midi_inbox.h` | `PeerMidiInbox`（子機の MIDI の受け口。ストリップごとの `MidiInputQueue` を持つ） |
| `vocroz/session/plugin_catalog.h` | `PluginCatalog`、`PluginScanProgress`（プラグインの一覧とスキャンの進行） |
| `vocroz/session/plugin_description.h` | `PluginDescription`（スキャンで見つけたプラグインの記述） |
| `vocroz/session/plugin_scan_record.h` | `PluginFile`、`PluginScanRecord`、`PluginScanStatus`、`PluginScanFailure` |
| `vocroz/session/ports/plugin_scanner.h` | `PluginScanner`（プラグインファイルの列挙とスキャン） |
| `vocroz/session/ports/plugin_catalog_store.h` | `PluginCatalogStore`（スキャンの記録の保存） |
| `vocroz/session/ports/plugin_loader.h` | `PluginLoader`、`PluginInstance`（プラグインの読み込みと状態の保存と復元） |
| `vocroz/session/ports/plugin_editor_host.h` | `PluginEditorHost`、`PluginEditorError`（プラグインのエディターのウィンドウ。`docs/adr/0041-plugin-editor-and-failure-notice.md`） |

テストでは `vocroz::session_fakes` をリンクすると、`fakes/fake_<役割名>.h` で各ポートのフェイクを使えます。

## スレッド

`HostSession` と `ClientSession` のメンバー関数は、すべてコントロールスレッドから呼びます。オーディオスレッドからは呼びません。
ネットワーク受信スレッド、デバイス、ランデブーサーバーとの通信の結果は、コンポジションルートがコントロールスレッドへ移してから渡します。
`tick()` は 100 ms〜1 s の間隔で呼び、招待の期限と再接続の猶予（120 秒）を進めます。
`PeerMidiInbox` だけは例外で、打鍵の遅れを増やさないよう、`on_midi_message()` を MIDI レーンを取り出したスレッドから session の通知を待たずに直接呼びます（内部はミューテックスで守り、オーディオスレッドへはキューで渡す）。`pa-engine` では信頼制御層がコントロールスレッドにあるので、コントロールスレッドから呼びます（`docs/adr/0028-engine-assembly.md`）。

## 組み立ての例（親機）

```cpp
// 受信: UI のメッセージはデコードしてから渡す。不正なものは渡さない。
if (auto message = control_protocol::decode_ui_to_engine(text)) {
    host_session_.handle_ui_message(message.value());
}
// 子機の制御メッセージは、信頼制御層から取り出した本文をそのまま渡す（検証は session が行う）。
host_session_.on_peer_message(peer, body);
```

### 子機の MIDI（親機）

`PeerMidiInbox` は承認の規則を持たず、`HostSession` が `PeerLink` と `MixerControl` に出した結果をそのまま受け取ります。
回線の途絶（`RECONNECTING`）でも Panic を求めるのは、復帰しないまま再接続の猶予（120 秒）のあいだ音が鳴り続けるのを防ぐためです（ADR-0029 の `LOST` での Panic より早い側に倒す）。

```cpp
// PeerLink の実装（コンポジションルート）: 承認と切断を MIDI の受け口にも伝える。
// routes は子機の入力ごとの流し込み先で、ストリップの並び順（先頭が子機のいちばん古いストリップ）。
void admit(PeerHandle peer, const std::vector<StreamRoute>& routes) override {
    media_.admit(peer, routes);
    // MIDI は子機ごとに 1 系統なので、先頭のストリップへ流す。ストリップのキューを返す。
    // グラフの MIDI ソースに使う
    midi_inbox_.admit(peer, routes.front().strip_id);
}
void close(PeerHandle peer) override { media_.close(peer); midi_inbox_.close(peer); }
// MixerControl::remove_channel() では midi_inbox_.remove_strip(strip_id)。
// 受信の途絶と復帰は host_session_.on_peer_timeout() / on_peer_resumed() と同じ所で
// midi_inbox_.suspend(peer) / resume(peer)。
// MIDI レーン（ControlLane::kMidi）の本文は、取り出したスレッドでそのまま渡す。
midi_inbox_.on_midi_message(peer, body);
```
