# transport

子機と親機の間のメディアの伝送を担うドメインのモジュールです。レイヤーはドメインで、OS の API は呼びません（AGENTS.md 3.3）。

## 責務

- Opus のエンコードとデコード（48kHz モノラル、`OPUS_APPLICATION_RESTRICTED_LOWDELAY`、2.5ms か 5ms のフレーム、DTX なし）と PLC（`docs/adr/0002-opus-low-delay.md`）
- 適応型ジッターバッファ（低遅延プロファイル。目標の深さは既定で 10〜20ms、上限と下限は設定で変えられる）。到着時刻ではなくメディアタイムスタンプで整列する
- クロックドリフト補正（深さを目標に保つ PI 制御と、変換比が 1 に近い可変レートのリサンプラー）
- 送信ミュートの世代番号（epoch）の追跡と、信号状態（`RECEIVING` / `PAUSED` / `RESUMING` / `NO_SIGNAL`）の判定

参加者と承認の判断は `session`、ソケットの実装は `network` が持ちます。

## 公開 API

| ヘッダー | 内容 | 呼び出すスレッド |
| --- | --- | --- |
| `vocroz/transport/constants.h` | `kSampleRate`、`FrameDuration`、`frame_samples()`、`kMaxOpusPayloadBytes` | どこからでも |
| `vocroz/transport/media_frame.h` | `MediaFrame`（タイムスタンプ、ミュートの世代番号、Opus のペイロード）、`make_media_frame()` | どこからでも |
| `vocroz/transport/opus_frame_encoder.h` | `OpusFrameEncoder`。`create()` はオーディオスレッド外、`encode()` はオーディオスレッドから呼び出し可能 | 子機のオーディオスレッド |
| `vocroz/transport/opus_frame_decoder.h` | `OpusFrameDecoder`。`decode()`、`conceal()`（PLC）、`reset()` | オーディオスレッド |
| `vocroz/transport/playout_stream.h` | `PlayoutStream`。1 ストリームの受信から PCM までをまとめたもの | `enqueue()` はネットワーク受信スレッド、`render()` はオーディオスレッド |
| `vocroz/transport/signal_monitor.h` | `SignalMonitor`。信号状態の判定と、古い世代のフレームの選別 | ネットワーク受信スレッド |
| `vocroz/transport/signal_state.h` | `SignalState`、`canonical_name()` | どこからでも |
| `vocroz/transport/send_mute_tracker.h` | `SendMuteTracker`。世代番号による送信ミュートの追跡 | 1 スレッドから |

### 受信側のつなぎ方

```cpp
// ネットワーク受信スレッド: 復号と検証が済んだフレームごとに
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

`audio-core`、`common`（AGENTS.md 4.2）。現在は `common` だけを使います。外部ライブラリは libopus（`Opus::opus`）を非公開でリンクし、公開ヘッダーに libopus の型を出しません。
