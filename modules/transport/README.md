# transport

子機と親機の間の UDP パケットの形式、Noise_IK による鍵交換、AEAD による暗号化、リプレイ排除と、その上の信頼制御層（制御メッセージの再送、キープアライブ、死活監視、再接続のバックオフ）を担うドメインのモジュールです。
ソケットの実装は持たず（`network` が `DatagramSocket` ポートを実装します）、バイト列の検証、暗号化、再送の判断だけを行います。
形式と暗号化の決定は [ADR-0001](../../docs/adr/0001-key-exchange-noise-ik.md) と [ADR-0014](../../docs/adr/0014-transport-wire-format.md)、信頼制御層の決定は [ADR-0019](../../docs/adr/0019-reliable-control-layer.md) にあります。

今後、Opus のエンコードとデコード、ジッターバッファ、クロックドリフト補正もこのモジュールに入ります（AGENTS.md 3.3）。

## 公開 API

| ヘッダー | 内容 | オーディオスレッド |
| --- | --- | --- |
| `vocroz/transport/constants.h` | パケットの最大長（1232 バイト）、鍵の長さ | — |
| `vocroz/transport/transport_error.h` | `TransportError`（破棄や失敗の原因） | — |
| `vocroz/transport/packet_header.h` | `PacketType`、`PacketHeader`、`parse_packet_header()`（復号前に接続 ID で届け先を選ぶ） | 呼び出し可能 |
| `vocroz/transport/media_payload.h` | `MediaPayload`（ストリーム ID、48kHz タイムスタンプ、ミュート epoch、Opus）、`decode_media_payload()` | 呼び出し可能 |
| `vocroz/transport/static_key_pair.h` | `StaticKeyPair`（Curve25519 の静的鍵。破棄時に秘密鍵を消す） | 呼び出さない |
| `vocroz/transport/noise_ik_handshake.h` | `NoiseIkHandshake`、`generate_connection_id()` | 呼び出さない |
| `vocroz/transport/secure_channel.h` | `PacketSealer`（送信）、`PacketOpener`（受信とリプレイ排除）、`SecureChannel` | 呼び出し可能 |
| `vocroz/transport/control_lane.h` | `ControlLane`（制御と MIDI のレーン）、制御メッセージの最大長 | — |
| `vocroz/transport/reliable_control_channel.h` | `ReliableControlChannel`（連番、選択 ACK、再送、キープアライブ）。コントロールスレッドで使う | 呼び出さない |
| `vocroz/transport/liveness_monitor.h` | `LivenessMonitor`（5 秒の受信途絶と再開の判定） | `note_packet_received()` だけ呼び出し可能 |
| `vocroz/transport/reconnect_backoff.h` | `ReconnectBackoff`、`reconnect_delay()`（1、2、4、8、15 秒、±20% のジッター） | 呼び出さない |
| `vocroz/transport/secure_control_packet_sink.h` | `SecureControlPacketSink`（制御パケットの平文を暗号化して `DatagramSocket` で送る） | 呼び出さない |
| `vocroz/transport/crypto_random_source.h` | `CryptoRandomSource`（libsodium による `RandomSource`） | 呼び出さない |
| `vocroz/transport/ports/` | `DatagramSocket`、`ControlPacketSink`、`RandomSource`（ポート）、`ControlMessageListener`、`LivenessListener`（リスナー） | — |

### 使い方

```cpp
using namespace vocroz::transport;

// 子機（開始側）。host_public は招待トークンから得た親機の公開鍵。
auto handshake = NoiseIkHandshake::create_initiator(client_keys, host_public, generate_connection_id().value()).value();
auto written = handshake.write_initiation(invite_token, packet);   // → 親機へ送る
auto read = handshake.read_response(received, payload);            // ← 親機から届いた応答
SecureChannel channel = std::move(handshake.finish()).value();

// 送信と受信。送信は 1 つのスレッド、受信はネットワーク受信スレッドで使う。
auto sealed = channel.sealer.seal_media(MediaPayload{...}, packet);
auto opened = channel.opener.open(received, plaintext);            // 改ざん・リプレイ・切り詰めは失敗で返る
auto media = decode_media_payload(opened.value().plaintext);
```

親機（応答側）は `create_responder()`、`read_initiation()`、`remote_static_public_key()` と招待トークンの検証（`session`）、`write_response()`、`finish()` の順に使います。

失敗したパケットは状態を変えずに捨てられます。認証に成功したパケットのシーケンス番号だけをリプレイ排除の窓に記録するので、偽造パケットで正規のパケットを捨てさせることはできません。

### 信頼制御層のつなぎ方

```cpp
// コントロールスレッド。制御だけを送る方向なら、暗号化して送る sink をそのまま使える。
SecureControlPacketSink sink(channel.sealer, socket);              // socket は network の DatagramSocket
ReliableControlChannel control(clock, sink, listener);             // listener は session の ControlMessageListener
LivenessMonitor liveness(clock, liveness_listener);                // 5 秒の途絶で on_peer_timed_out()

auto sent = control.send(ControlLane::kControl, message);          // 到達と送信順を保証する

// ネットワーク受信スレッド。認証に成功したパケットごとに死活監視を更新し、
// 制御パケットの平文はコントロールスレッドへ渡す。
liveness.note_packet_received();
// コントロールスレッド
auto applied = control.on_control_packet(opened.plaintext);        // ACK の適用、重複の排除、整列
// next_deadline() の時刻に呼ぶ
control.tick();                                                    // 再送とキープアライブ
liveness.poll();
```

再接続を始めたら、`ReconnectBackoff::next_delay()` の時間だけ待って試行し、成功したら `reset()` します。

## 依存してよいモジュール

`audio-core`、`common`（AGENTS.md 4.2）。外部ライブラリは libsodium と noise-c をこのモジュールの中だけで使い、公開ヘッダーには型を出しません。
