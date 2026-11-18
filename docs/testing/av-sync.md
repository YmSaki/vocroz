# A/V 同期の試験パターンと計測

## この文書について

Issue #144 の試験ツールで、Beta6 の音声と映像の同期を数値で確認します。CI の `av_sync_tests` では既知のずれを入れたファイルを生成し、計測値と照合します。

## パターンを作る

```sh
build/tests/tools/vocroz-av-sync generate --out-dir /tmp/av-sync-check --audio-offset-ms 40
```

| 引数 | 意味 | 既定値 |
| --- | --- | --- |
| `--out-dir` | 2 ファイルを書き出すディレクトリ | 必須 |
| `--seconds` | 全体の長さ（秒） | 10 |
| `--fps` | 毎秒のフレーム数 | 60 |
| `--interval-ms` | 発光間隔（ms） | 1000 |
| `--audio-offset-ms` | 音を遅らせる時間（ms）。負なら早めます | 0 |

生成に成功すると、4:2:0 の映像 `av-sync-pattern.y4m` と 16 ビットモノラル PCM の音声 `av-sync-pattern.wav` ができます。既定のパターンは 1 秒目から光ります。

## ずれを測る

```sh
build/tests/tools/vocroz-av-sync measure --video /tmp/av-sync-check/av-sync-pattern.y4m --audio /tmp/av-sync-check/av-sync-pattern.wav
```

```text
pairs=9
unmatched_video=0
unmatched_audio=0
min_ms=40.02
max_ms=40.02
mean_ms=40.02
median_ms=40.02
p95_abs_ms=40.02
```

光った時刻を音の立ち上がり時刻から引いています。正の値は音が遅れたことを示します。

## 結果の読み方

| 項目 | 意味 |
| --- | --- |
| `pairs` | 対応づけた光と音の組数 |
| `unmatched_video` | 音が対応しなかった光の数 |
| `unmatched_audio` | 光が対応しなかった音の数 |
| `min_ms`、`max_ms` | ずれの最小値と最大値 |
| `mean_ms`、`median_ms` | ずれの平均値と中央値 |
| `p95_abs_ms` | ずれの絶対値の 95 パーセンタイル |

対応しなかったイベントがあれば、光や音の取りこぼし、録画の輝度や音量がしきい値に届いているかを確認します。映像の時間分解能は 1 フレームで、60 fps なら約 16.7 ms です。

## まだできないこと

MP4 などの録画ファイルの読み取りは、Issue #136 の ADR でコンテナが決まってから追加します。それまでは同期を保って Y4M と WAV に書き出してから測ります。

高負荷を作る手段と XRUN・フレームドロップの記録方法は後半で扱います。画面にパターンを表示するには、Y4M を扱えるプレーヤーを使います。再生映像をキャプチャした場合、プレーヤー自身の映像と音声の開始時刻の差も結果に含まれます。
