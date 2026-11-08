# control-protocol

UI とエンジンの間でやり取りするメッセージを 1 か所で定義するモジュールです。レイヤーはドメインで、依存してよいのは `common` だけです（AGENTS.md 4.2）。
メッセージの形、revision の進め方、検証の範囲は `docs/adr/0015-control-protocol-messages.md` で、ルーティングのメッセージは `docs/adr/0026-routing-staging.md` で決めています。

## 責務

- コマンド（UI からエンジン）とイベント（エンジンから UI）の DTO
- JSON のエンコードとデコード。デコードは信頼境界で、不正な入力を拒否して検証済みの型だけを返す
- メッセージを運ぶ `MessageChannel` ポートと、2 つの経路をつなぐ `ChannelRelay`

ビジネスロジックと状態（コマンドの適用、revision の管理、entity ごとの値の範囲）は `session` が持ちます。

## 公開 API

| ヘッダー | 内容 |
| --- | --- |
| `vocroz/control_protocol/states.h` | `SessionState`、`ConnectionState`、`SignalState`、`OutputDeviceState`、`MasterMuteReason`、`ClientState`、`ErrorCode`、`NoticeSeverity`、ルーティングの `BusKind`、`SendTap`、`RoutingIssue` |
| `vocroz/control_protocol/entities.h` | 状態の DTO（`SessionInfo`、`ChannelStrip`、`MasterStrip`、`PendingParticipant`、`MixerSnapshot`、`ClientSnapshot`、ルーティングの `RoutingConfig`、`RoutingInfo`、`RouteLevel` など）と `Revision`、`Sequence`、`GainDb` |
| `vocroz/control_protocol/commands.h` | `UiToEngineMessage`（`Hello`、`DiscreteCommand`、`SetGain`） |
| `vocroz/control_protocol/events.h` | `EngineToUiMessage`（スナップショット、差分イベント、`Ack`、`Reject`、`GainAck`、`Meters`、`Notice`） |
| `vocroz/control_protocol/json_codec.h` | `decode_ui_to_engine()`、`encode_ui_to_engine()`、`decode_engine_to_ui()`、`encode_engine_to_ui()`、`DecodeError` |
| `vocroz/control_protocol/constants.h` | `kMaxMessageBytes`、`kMinFiniteGainDb`、`kMaxGainDb`、`kMaxRoutingDelayMs`、`kMaxCounter` |
| `vocroz/control_protocol/ports/message_channel.h` | `MessageChannel` ポート（`ipc` と `webui` が実装する） |
| `vocroz/control_protocol/ports/message_listener.h` | `MessageListener` |
| `vocroz/control_protocol/channel_relay.h` | `ChannelRelay`（親機モードの `remote-pa` で、WebView と `pa-engine` をつなぐ） |

どの関数もオーディオスレッドからは呼び出しません（ヒープを確保するため）。

### エンジン側での使い方

```cpp
void EngineSession::on_message(std::string_view text) {
    const auto message = control_protocol::decode_ui_to_engine(text);
    if (!message) {
        // 不正な入力は適用しない。request_id も信用できないので Reject も返さない。
        return;
    }
    std::visit([this](const auto& m) { apply(m); }, message.value());
}
```

## メッセージを追加するとき

次をすべて同じ PR で変更します（AGENTS.md 4.4）。

1. `commands.h` か `events.h` に型を足し、`UiToEngineMessage` か `EngineToUiMessage` に加える
2. `src/wire/message_types.h` に種別の名前を、`src/wire/command_codec.cpp` か `src/wire/event_codec.cpp` に `write_fields()` と `read_fields()` を足す
3. `tests/data/` の例に足す（往復のテストが、全種別を含むことを確かめる）
4. `ui/src/ipc/messages.ts` の型を合わせる

## テストダブル

`tests/fakes/fake_message_channel.h` の `FakeMessageChannel` は、CMake ターゲット `vocroz::control_protocol_fakes` で `ipc` と `webui` のテストからも使えます。

## 依存してよいモジュール

`common`。外部ライブラリは nlohmann/json を実装の中だけで使い、公開ヘッダーには出しません。
