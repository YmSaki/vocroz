# transport

子機と親機の間の UDP パケットの形式、Noise_IK による鍵交換、AEAD による暗号化、リプレイ排除と、受信したメディアを PCM に戻す処理（Opus、ジッターバッファ、クロックドリフト補正、信号状態）、信頼制御層（制御メッセージの再送、キープアライブ、死活監視、再接続のバックオフ）を担うドメインのモジュールです。
外部の受信機へ送る SRTP 外部出力の RTP の組み立てと SRTP の暗号化も担います（子機と親機の間の形式とは別の部品）。
ソケットの実装は持ちません（`network` が `DatagramSocket` ポートを実装します）。
形式と暗号化の決定は [ADR-0001](../../docs/adr/0001-key-exchange-noise-ik.md) と [ADR-0014](../../docs/adr/0014-transport-wire-format.md)、信頼制御層の決定は [ADR-0019](../../docs/adr/0019-reliable-control-layer.md) にあります。

## 公開 API

| ヘッダー | 内容 | 呼ぶスレッド |
| --- | --- | --- |
| `vocroz/transport/constants.h` | パケットの最大長（1232 バイト）、鍵の長さ、`kSampleRate`、`FrameDuration`、`frame_samples()`、`kMaxOpusPayloadBytes`、`kReturnStreamId`（返送の下りストリーム ID） | — |
| `vocroz/transport/transport_error.h` | `TransportError`（破棄や失敗の原因） | — |
| `vocroz/transport/packet_header.h` | `PacketType`、`PacketHeader`、`parse_packet_header()`（復号前に接続 ID で届け先を選ぶ） | どこからでも（ヒープ確保なし） |
| `vocroz/transport/media_payload.h` | `MediaPayload`（ストリーム ID、48kHz タイムスタンプ、ミュート epoch、Opus）、`decode_media_payload()` | どこからでも（ヒープ確保なし） |
| `vocroz/transport/static_key_pair.h` | `StaticKeyPair`（Curve25519 の静的鍵。破棄時に秘密鍵を消す） | コントロール |
| `vocroz/transport/noise_ik_handshake.h` | `NoiseIkHandshake`、`generate_connection_id()` | コントロールかネットワーク受信 |
| `vocroz/transport/secure_channel.h` | `PacketSealer`（送信）、`PacketOpener`（受信とリプレイ排除）、`SecureChannel` | 送信はネットワーク送信側、受信はネットワーク受信（ヒープ確保、ロック、I/O なし） |
| `vocroz/transport/media_frame.h` | `MediaFrame`（タイムスタンプ、ミュートの世代番号、Opus のペイロード）、`make_media_frame()` | どこからでも |
| `vocroz/transport/opus_frame_encoder.h` | `OpusFrameEncoder`（モノラルかステレオ。ステレオはインターリーブの PCM）。`create()` はオーディオスレッド外、`encode()` はオーディオスレッドから呼び出し可能 | 子機のオーディオスレッド |
| `vocroz/transport/media_stream_encoder.h` | `MediaStreamEncoder`（`AudioSink` の実装。送信レベルとミュートのランプ、世代番号、Opus のエンコード、ミュート中の送信停止）、`StreamSendParams` | `set_params()` はコントロール、`push()` はオーディオスレッド |
| `vocroz/transport/opus_frame_decoder.h` | `OpusFrameDecoder`（モノラルかステレオ）。`decode()`、`conceal()`（PLC）、`reset()` | オーディオスレッド |
| `vocroz/transport/playout_stream.h` | `PlayoutStream`。1 ストリームの受信から PCM までをまとめたもの。`set_max_delay()` で目標遅延（ジッターバッファの上限）を変える | `enqueue()` はネットワーク受信スレッド、`render()` はオーディオスレッド、`set_max_delay()` はどこからでも（次の `render()` で反映） |
| `vocroz/transport/jitter_estimator.h` | `JitterEstimator`。到着間隔の揺らぎ（RFC 3550 の interarrival jitter）の推定 | ネットワーク受信スレッド（ヒープ確保なし） |
| `vocroz/transport/signal_monitor.h` | `SignalMonitor`。信号状態の判定と、古い世代のフレームの選別 | ネットワーク受信スレッド |
| `vocroz/transport/signal_state.h` | `SignalState`、`canonical_name()` | どこからでも |
| `vocroz/transport/send_mute_tracker.h` | `SendMuteTracker`。世代番号による送信ミュートの追跡 | 1 スレッドから |
| `vocroz/transport/control_lane.h` | `ControlLane`（制御と MIDI のレーン）、制御メッセージの最大長 | どこからでも（定数と列挙型だけ） |
| `vocroz/transport/reliable_control_channel.h` | `ReliableControlChannel`（連番、選択 ACK、再送、キープアライブ） | コントロール |
| `vocroz/transport/liveness_monitor.h` | `LivenessMonitor`（5 秒の受信途絶と再開の判定） | 記録はネットワーク受信（ヒープ確保、ロックなし）、判定はコントロール |
| `vocroz/transport/reconnect_backoff.h` | `ReconnectBackoff`、`reconnect_delay()`（1、2、4、8、15 秒、±20% のジッター） | コントロール |
| `vocroz/transport/secure_control_packet_sink.h` | `SecureControlPacketSink`（制御パケットの平文を暗号化して `DatagramSocket` で送る） | sealer を使うスレッド |
| `vocroz/transport/srtp_master_key.h` | `SrtpMasterKey`（SRTP 外部出力のマスターキーとソルト。破棄時に消去）、`parse_srtp_master_key()`（SDES の `inline:` 形式のパース。信頼境界）、`format_srtp_master_key()`、`generate_srtp_master_key()` | どこからでも |
| `vocroz/transport/external_stream_sender.h` | `ExternalStreamSender`（RTP の組み立てと SRTP の暗号化と送信。平文は送らない）、`SrtpCryptoSuite`、`RtpStreamParams`、`make_rtp_stream_params()`、`ExternalStreamError` | `create()` はコントロール、`send()` は送信スレッド（オーディオスレッドからは呼ばない） |
| `vocroz/transport/peer_packet_sender.h` | `PeerPacketSender`（1 つの接続の送信方向。制御パケットの平文とエンコード済みのフレームを SPSC キューで受け取り、`pump()` で暗号化して送る） | 積むのはコントロールとオーディオスレッド、`pump()` は送信スレッド |
| `vocroz/transport/send_wakeup.h` | `SendWakeup`（`SendNotifier` の実装。送信スレッドを合図で起こす） | `notify()` はどこからでも、`wait()` は送信スレッド |
| `vocroz/transport/crypto_random_source.h` | `CryptoRandomSource`（libsodium による `RandomSource`） | どこからでも |
| `vocroz/transport/ports/` | `DatagramSocket`、`ControlPacketSink`、`RandomSource`、`MediaFrameSink`、`SendNotifier`（ポート）、`ControlMessageListener`、`LivenessListener`（リスナー） | 各ヘッダーの説明のとおり |

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

### 送信側のつなぎ方（親機から子機への返送）

親機から子機への返送（下りストリーム）は、上りと同じ `MediaPayload` で運び、下りのストリーム ID 0 を使います（[ADR-0036](../../docs/adr/0036-return-monitor.md)）。

```cpp
// コントロールスレッド: エンコーダーを作り、audio-core のグラフの返送タップの渡し先にする。
auto encoder = std::make_shared<MediaStreamEncoder>(OpusFrameEncoder::create({}).value(), frame_sink);
engine_mixer.attach_return(strip_id, encoder);
// 返送のミュートを切り替えるたび（session の PeerLink::set_return_mute()）。
(void)encoder->set_params({muted, epoch, 1.0F});
// オーディオスレッド: グラフの返送タップが push() を呼び、1 フレームごとに frame_sink.push_frame()。
// ネットワークのスレッド: frame_sink から取り出して seal_media() で暗号化して送る。
```

ミュートすると、ランプで無音にしてから送信を止めます。止めている間もタイムスタンプは進めるので、再開後のフレームは実時間どおりの位置に並びます。

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

### SRTP 外部出力のつなぎ方

外部の放送機器やサーバーへ、標準の RTP / SRTP（RFC 3550、RFC 3711、RFC 7587）で送ります。子機と親機の間の形式（`PacketHeader`、`SecureChannel`）とは部品も鍵も共有しません（[ADR-0040](../../docs/adr/0040-srtp-external-output.md)）。

```cpp
// コントロールスレッド: 鍵は受信機の設定から貼り付けた inline: の文字列か、generate_srtp_master_key() で作る。
auto key = parse_srtp_master_key(text);                            // 不正な鍵、すべて 0、寿命や MKI の指定は失敗
CryptoRandomSource random;
auto params = make_rtp_stream_params(random, kDefaultOpusPayloadType, FrameDuration::k5000Us);
auto sender = ExternalStreamSender::create(SrtpCryptoSuite::kAesCm128HmacSha1_80, key.value(), params, socket);
// 送信スレッド: オーディオスレッドでエンコードしたフレームを SPSC キューから取り出して送る。
auto sent = sender.value().send(frame);                            // 暗号化に失敗したら送らずに捨てる
```

鍵がなければ `ExternalStreamSender` を作れないので、平文の RTP を送る経路はありません。マーカービットは最初のパケットと、タイムスタンプが 1 フレームより先へ飛んだ後（送信の再開）に立てます。

## 依存してよいモジュール

`audio-core`、`common`（AGENTS.md 4.2）。`audio-core` は `VOCROZ_NONBLOCKING`（`vocroz/audio_core/realtime.h`）と、`MediaStreamEncoder` が実装する `AudioSink` ポートとゲインランプに使います。外部ライブラリは libsodium、noise-c、libopus、libsrtp をこのモジュールの中だけで使い、公開ヘッダーには型を出しません。
オーディオスレッドから呼ぶ関数（`PlayoutStream::render()` とそこから呼ばれる関数、`OpusFrameEncoder::encode()`、`MediaStreamEncoder::push()`）には、`audio-core` の `VOCROZ_NONBLOCKING` を付けています（AGENTS.md 第 9 節）。
