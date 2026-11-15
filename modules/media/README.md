# media

録音のファイルの書き出し（WAV、FLAC）と、再生するファイルの読み込みとデコードを、JUCE で実装するアダプター
（`docs/adr/0043-recording-and-media-player.md`）。
`audio-core` の `AudioFileWriter` と `AudioFileReader` のポートを実装する。

## 責務

- 録音のファイルを 48 kHz、24 bit の WAV か FLAC で書く。WAV は 4 GiB を超えたら RF64 で書く
- WAV は音声の 1 秒ごとにヘッダーを書き直し、エンジンが異常終了しても直前の 1 秒までを再生できるようにする
- ボリュームの空きが 64 MiB を下回る前に書き出しを止め、`kDiskFull` を返す。書けたところまでは閉じて残す
- WAV、AIFF、FLAC、Ogg Vorbis を読み、48 kHz に変換して返す。3 チャンネル以上のファイルは先頭の 2 チャンネルだけを使う

持たない責務: リングバッファとオーディオスレッドとの受け渡し（`audio-core`）、録音の状態と失敗の扱い（`session`）、
ディスク I/O スレッド（`pa-engine`）。

## 公開 API

| ヘッダー | 内容 |
| --- | --- |
| `vocroz/media/factories.h` | `open_audio_file_writer()`、`open_audio_file_reader()`。JUCE を使うので `VOCROZ_WITH_JUCE` が ON のときだけ定義がある |

## 依存してよいモジュール

`audio-core`、`common`（AGENTS.md 4.2）。外部ライブラリは JUCE（`juce_audio_formats`。FLAC は JUCE に内蔵の libFLAC）。

## テスト

- `tests/free_space_guard_test.cpp`: 空き容量を調べる間隔と下限の判断（JUCE なしで Linux の CI でも動く）
- `tests/juce_audio_file_test.cpp`: 一時フォルダに実際のファイルを書いて読み戻す（形式ごとの往復、上書きの禁止、
  容量不足での停止、書き出し中のヘッダーの書き直し、44.1 kHz のファイルの変換、シーク、多チャンネルのファイル）。
  JUCE が要るので Windows の CI で動く
