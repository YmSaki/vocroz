# transport

子機と親機の間の UDP パケットの形式、Noise_IK による鍵交換、AEAD による暗号化、リプレイ排除を担うドメインのモジュールです。
ソケットは持たず、バイト列の検証と暗号化だけを行います（ソケットの実装は `network`）。
形式と暗号化の決定は [ADR-0001](../../docs/adr/0001-key-exchange-noise-ik.md) と [ADR-0014](../../docs/adr/0014-transport-wire-format.md) にあります。

今後、信頼制御層、Opus のエンコードとデコード、ジッターバッファ、クロックドリフト補正もこのモジュールに入ります（AGENTS.md 3.3）。

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

## 依存してよいモジュール

`audio-core`、`common`（AGENTS.md 4.2）。外部ライブラリは libsodium と noise-c をこのモジュールの中だけで使い、公開ヘッダーには型を出しません。
