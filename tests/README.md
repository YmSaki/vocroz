# tests: 結合テストと試験基盤

モジュールをまたぐ結合テストと、その試験基盤と試験ツールを置きます。
製品のターゲットからはリンクせず、インストーラーにも入れません。
構成と判断の理由は [docs/adr/0025-integration-test-harness.md](../docs/adr/0025-integration-test-harness.md) にあります。

| ディレクトリ | 中身 |
| --- | --- |
| `netsim/` | 障害の設定（`FaultProfile`）と判定（`FaultInjector`）、プロセス内のネットワーク（`SimulatedNetwork`） |
| `harness/` | transport の部品を製品と同じ順序で組み立てた送信側と受信側、session をつないだ親機と子機（`SimulatedHost`、`SimulatedClient`）、仮想時刻の駆動、遅延計測 |
| `integration/` | 結合テスト（GoogleTest。`ctest` で動く） |
| `tools/` | `vocroz-latency`（遅延の見積もり）と `vocroz-udp-fault-proxy`（実ソケットの障害注入、Linux だけ） |
| `nat/` | ネットワーク名前空間で NAT を模し、ホールパンチの成否を確かめるスクリプト |

## 結合テスト

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure -R 'Ensemble|MediaFaults|ControlFaults|Latency|SimulatedNetwork|FaultInjector|FaultProfile|UdpFaultProxy'
```

結合テストは仮想時刻で動きます。実時間を待たず、OS もデバイスも要らないので、数秒分の通信も 1 秒以内に終わり、何度動かしても同じ結果になります。

| テスト | 確かめること | 受け入れテスト |
| --- | --- | --- |
| `MediaFaultsTest.KeepsPlayingThroughReorderDuplicateLossAndJitter` | 順序の入れ替え、重複、欠損、ジッターでも破綻せず、音が出続ける | AT-A06 |
| `MediaFaultsTest.GoesSilentAfter500msOutageAndRecovers` | 500ms 以上の途絶で `NO_SIGNAL` になって無音になり、回線が戻れば音も戻る | AT-A06 |
| `MediaFaultsTest.DoesNotPlayStaleAudioReleasedAfterStall` | 経路が詰まってまとめて届いた古い音を、遅れて鳴らし続けない | AT-A06 |
| `MediaFaultsTest.DropsTamperedPacketsAtDecryption` | 改ざんしたパケットをすべて復号の段階で捨て、音に影響しない | AT-A17 |
| `ControlFaultsTest.DeliversEveryMessageOnceInOrderOverLossyLink` | 欠損 20% の回線でも、制御メッセージがすべて 1 回ずつ送った順に届く | AT-A02、A05 の前提 |
| `LatencyTest.*` | 端から端までの遅延が、構成から計算した内訳の範囲に収まる | 合奏の低遅延 |
| `EnsembleTest.BlocksAudioUntilApproved` | 承認前の子機の音は、子機が送信ミュートを無視して送ってきても出力に出ない。承認した子機の音だけがミックスに入る | AT-A02 |
| `EnsembleTest.MixesThreeClientsAndAppliesChannelAndMasterMute` | 3 台の音（440、660、880Hz）がそれぞれのストリップを通って独立にミックスされ、チャンネルのミュートはその 1 台だけを、Master のミュートは全体を無音にする | AT-A03 |
| `EnsembleTest.KeepsOthersPlayingThroughDisconnectAndRejoinsSameStrip` | 1 台の回線が 7 秒途絶えても残りの 2 台の音は途切れず、戻った子機は承認なしで同じストリップに戻る | AT-A05 |

`EnsembleTest` は、session の `HostSession` と `ClientSession` を、transport の部品（Noise_IK、暗号化、信頼制御層、死活監視、Opus、ジッターバッファ）とシミュレーションのネットワークでつなぎ、招待から参加、承認、ミュートの解除、切断と復帰までを UI のコマンドだけで進めます。
ランデブーサーバーは、招待の登録と解決にその場で答える代わりの実装でつなぎます（実際のサーバーとの通しは `tests/nat`）。
ミキサーとデバイスは試験用の簡単な実装（`HarnessMixer`）で、pa-engine のミキサーとリミッターは通しません。pa-engine と client-desktop のコンポジションルートができたら、`SimulatedHost` と `SimulatedClient` をそちらの組み立てに置き換えます。

## 障害の設定の書き方

結合テスト、`vocroz-latency`、`vocroz-udp-fault-proxy` で同じ書き方を使います。`key=value` をカンマで区切り、書かなかった項目は既定値（障害なし）のままです。

| キー | 意味 | 例 |
| --- | --- | --- |
| `delay` | 片道の固定の遅延 | `delay=20ms` |
| `jitter` | 遅延に加える揺らぎの幅（0 以上この値未満の一様分布） | `jitter=5ms` |
| `loss` | 欠損の確率（0〜1） | `loss=0.02` |
| `duplicate` | 重複の確率。2 つ目は `reorder_delay` だけ遅れて届く | `duplicate=0.01` |
| `reorder` | 余分に遅らせて後続に追い越させる確率 | `reorder=0.05` |
| `reorder_delay` | 順序の入れ替えと重複の 2 つ目に加える遅延（既定 10ms） | `reorder_delay=8ms` |
| `corrupt` | 1 ビットを反転させる確率 | `corrupt=0.001` |
| `seed` | 乱数のシード。同じシードと同じ送信の順序なら同じ障害になる | `seed=7` |

時間の単位は `us`、`ms`、`s` です。乱数は環境によらず同じ列になるので、CI で失敗した条件を Windows の手元でもそのまま再現できます。

## 遅延の見積もり（vocroz-latency）

実際の伝送の部品（Opus、暗号化、ジッターバッファ、ドリフト補正）を通した遅延を、指定した回線の条件で計り、構成から計算した内訳と並べて表示します。

```sh
build/tests/tools/vocroz-latency --net delay=15ms,jitter=4ms,loss=0.01 \
  --frame 5 --min-delay 10 --max-delay 20 --capture-block 128 --output-block 128 \
  --device-in 3 --device-out 3 --seconds 10
```

| 引数 | 意味（既定値） |
| --- | --- |
| `--net` | 子機から親機への回線の障害（障害なし） |
| `--frame` | フレーム長 ms、`2.5` か `5`（5） |
| `--min-delay`、`--max-delay` | ジッターバッファの目標の下限と上限 ms（10、20） |
| `--capture-block`、`--output-block` | デバイスのコールバックのブロック長（サンプル数、128） |
| `--device-in`、`--device-out` | デバイスの入出力の遅延 ms。シミュレーションの外なので足すだけ（0） |
| `--seconds` | 計測の長さ（10） |

無音の間に 1kHz のバーストを約 400ms ごとに送り、出力での立ち上がりと入力での立ち上がりの差を遅延とします。
計測値が内訳の上限（`total max`）を超えたら、どこかに想定外のバッファがあります。
実機の値は [docs/testing/alpha-acceptance.md](../docs/testing/alpha-acceptance.md) の「参考の測定」の方法で録音して確かめ、この見積もりと並べて記録します。

## 実ソケットの障害注入（vocroz-udp-fault-proxy、Linux）

子機と上流（親機かランデブーサーバー）の間に置く UDP のプロキシです。子機は上流の代わりにプロキシのアドレスへ送ります。

```sh
build/tests/tools/vocroz-udp-fault-proxy --listen 0.0.0.0:40001 --upstream 192.168.1.10:40000 \
  --to-upstream delay=20ms,jitter=5ms,loss=0.02,seed=7 --to-client delay=20ms
```

子機の送信元ごとに上流側のソケットを分けるので、上流からは子機ごとに別の送信元に見えます。Ctrl+C で終了し、統計を表示します。
Windows の実機確認では、WSL か、経路の途中に置いた Linux の機械で動かします（Windows だけで済ませる場合は、手順書のとおり clumsy を使います）。

## NAT の模擬（tests/nat）

Linux のネットワーク名前空間と iptables で NAT の種類を模し、ランデブーサーバー越しの UDP ホールパンチの成否を組み合わせごとに確かめます。root 権限と Go が要ります。

```sh
sudo tests/nat/run-nat-matrix.sh
```

| 子機側の NAT | 親機側の NAT | 期待する結果 |
| --- | --- | --- |
| フルコーン | フルコーン | `CONNECTED` |
| ポート制限 | ポート制限 | `CONNECTED` |
| ポート制限 | フルコーン | `CONNECTED` |
| 対称型 | フルコーン | `CONNECTED` |
| 対称型 | なし（直結） | `CONNECTED` |
| 対称型 | ポート制限 | `UNREACHABLE` |
| 対称型 | 対称型 | `UNREACHABLE` |
| ポート制限（サーバーなし） | ポート制限 | `RENDEZVOUS_UNREACHABLE` |
| ポート制限（親機なし） | | `PEER_NOT_FOUND` |

親機と子機の代わりに、試験用のピア `punchprobe`（`infra/rendezvous/cmd/punchprobe`）がランデブーサーバーと同じワイヤーフォーマットで REGISTER を送り、PUNCH で受け取った相手の候補へ同時に送信します。
結果の意味は ADR-0025 の表のとおりです。session と network のクライアントができたら、`punchprobe` を製品のクライアントに置き換えます。
