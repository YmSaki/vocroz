# transport

子機と親機の間の UDP パケットの形式、Noise_IK による鍵交換、AEAD による暗号化、リプレイ排除と、受信したメディアを PCM に戻す処理（Opus、ジッターバッファ、クロックドリフト補正、信号状態）を担うドメインのモジュールです。
ソケットは持ちません（ソケットの実装は `network`）。
形式と暗号化の決定は [ADR-0001](../../docs/adr/0001-key-exchange-noise-ik.md) と [ADR-0014](../../docs/adr/0014-transport-wire-format.md) にあります。

今後、信頼制御層もこのモジュールに入ります（AGENTS.md 3.3）。

## 公開 API

| ヘッダー | 内容 | 呼ぶスレッド |
| --- | --- | --- |
| `vocroz/transport/constants.h` | パケットの最大長（1232 バイト）、鍵の長さ、`kSampleRate`、`FrameDuration`、`frame_samples()`、`kMaxOpusPayloadBytes` | — |
| `vocroz/transport/transport_error.h` | `TransportError`（破棄や失敗の原因） | — |
| `vocroz/transport/packet_header.h` | `PacketType`、`PacketHeader`、`parse_packet_header()`（復号前に接続 ID で届け先を選ぶ） | どこからでも（ヒープ確保なし） |
| `vocroz/transport/media_payload.h` | `MediaPayload`（ストリーム ID、48kHz タイムスタンプ、ミュート epoch、Opus）、`decode_media_payload()` | どこからでも（ヒープ確保なし） |
| `vocroz/transport/static_key_pair.h` | `StaticKeyPair`（Curve25519 の静的鍵。破棄時に秘密鍵を消す） | コントロール |
| `vocroz/transport/noise_ik_handshake.h` | `NoiseIkHandshake`、`generate_connection_id()` | コントロールかネットワーク受信 |
| `vocroz/transport/secure_channel.h` | `PacketSealer`（送信）、`PacketOpener`（受信とリプレイ排除）、`SecureChannel` | 送信はネットワーク送信側、受信はネットワーク受信（ヒープ確保、ロック、I/O なし） |
| `vocroz/transport/media_frame.h` | `MediaFrame`（タイムスタンプ、ミュートの世代番号、Opus のペイロード）、`make_media_frame()` | どこからでも |
| `vocroz/transport/opus_frame_encoder.h` | `OpusFrameEncoder`。`create()` はオーディオスレッド外、`encode()` はオーディオスレッドから呼び出し可能 | 子機のオーディオスレッド |
| `vocroz/transport/opus_frame_decoder.h` | `OpusFrameDecoder`。`decode()`、`conceal()`（PLC）、`reset()` | オーディオスレッド |
| `vocroz/transport/playout_stream.h` | `PlayoutStream`。1 ストリームの受信から PCM までをまとめたもの | `enqueue()` はネットワーク受信スレッド、`render()` はオーディオスレッド |
| `vocroz/transport/signal_monitor.h` | `SignalMonitor`。信号状態の判定と、古い世代のフレームの選別 | ネットワーク受信スレッド |
| `vocroz/transport/signal_state.h` | `SignalState`、`canonical_name()` | どこからでも |
| `vocroz/transport/send_mute_tracker.h` | `SendMuteTracker`。世代番号による送信ミュートの追跡 | 1 スレッドから |

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

### 受信側のつなぎ方

```cpp
// ネットワーク受信スレッド: 復号した MediaPayload を make_media_frame() で MediaFrame にしてから
if (monitor.on_media_frame(frame.mute_epoch, clock.now())) {
    stream->enqueue(frame);
}
// 制御メッセージで送信ミュートの通知を受けたとき
monitor.on_send_mute(send_muted, mute_epoch, clock.now());
// 100ms 程度の間隔で
monitor.poll(clock.now());

// オーディオスレッド: デバイスのコールバックごとに（長さは毎回変わってよい）
stream->render(block);
```

### 振る舞いの要点

- 再生の開始は、深さが目標に達してからにする。開始時は 10ms でフェードインする。
- 欠損は PLC で補う。フレームが途絶えたら PLC を 40ms まで続けてフェードアウトし、無音にして溜め直す。古い音を引き延ばして鳴らし続けない。
- 再生位置より古いフレーム、重複、すでに PLC で埋めた位置のフレームは捨てる。回線の停滞のあとにまとめて届き、深さが上限を超えたら、古いほうを捨てて目標まで浅くする。
- 枯渇したら目標を 1 フレーム深くする。10 秒間余裕が残り続けたら 1 フレーム浅くする。
- ミュートの世代番号が新しいフレームが届いたら、新しい区間としてバッファとデコーダーを初期化する。ドリフトの推定値は区間をまたいで保持する。

### ミュートの世代番号の規則

子機は送信ミュートを解除するたびに世代番号を 1 増やし、その番号をフレームに載せて送ります。ミュート中は送信しません。
送信ミュートの通知も `(send_muted, 世代番号)` を運びます。同じ世代では「解除 → ミュート」の順に起きるので、受信側は `(世代番号, ミュート中か)` の組の辞書順で新旧を比べ、到着順が逆転した通知とミュート前のフレームを捨てます。
解除の通知より先に新しい世代のフレームが届いた場合は、解除を知ったものとして扱います。

## 依存してよいモジュール

`audio-core`、`common`（AGENTS.md 4.2）。`audio-core` は `VOCROZ_NONBLOCKING`（`vocroz/audio_core/realtime.h`）のためだけに使います。外部ライブラリは libsodium、noise-c、libopus をこのモジュールの中だけで使い、公開ヘッダーには型を出しません。
オーディオスレッドから呼ぶ関数（`PlayoutStream::render()` とそこから呼ばれる関数、`OpusFrameEncoder::encode()`）には、`audio-core` の `VOCROZ_NONBLOCKING` を付けています（AGENTS.md 第 9 節）。
