# VOCROZ

VOCROZ は、インターネット越しに複数の出演者の音声を集め、1 台の操作卓（PA）でリアルタイムにミキシングする Windows 向けのデスクトップアプリです。
離れた場所にいる演奏者どうしの合奏のように、遅延を小さく抑える必要がある用途を想定しています。

> **状況**: 開発の初期段階です。現在は Alpha の最初のフェーズ（開発基盤の整備と技術検証）を進めています。予定は下の「[今後の予定](#今後の予定)」を参照してください。

## 構成

UI が固まったりクラッシュしたりしても音声処理が止まらないように、役割ごとにプロセスを分けています。

| 実行ファイル | 役割 |
| :--- | :--- |
| `remote-pa` | 操作卓の画面（WebView2 と TypeScript）。`--client` を付けると出演者用のクライアントとして動き、マイク音声を Opus で符号化して送信する |
| `pa-engine` | 親機の音声エンジン。セッション、参加者の権限、独自の UDP 通信、Opus の復号、ジッターバッファ、ミキサーを受け持つ |
| `pa-updater` | セッションが終わった後にだけ起動する短命のプロセス。更新ファイルの署名を検証してから適用する |
| `infra/rendezvous` | 招待の解決、STUN、P2P 接続のためのアドレス交換を行う Go 製のサーバー。**音声の処理も中継もしない** |

設計判断は [docs/adr](docs/adr/) に、開発ルール（設計原則、デザインパターン、コーディング規則）は [AGENTS.md](AGENTS.md) にまとめています。

## 今後の予定

1. **Alpha**: 招待と承認、WAN 越しの低遅延な音声伝送、8 チャンネル以上のミキサー、インストーラーと更新
2. **Beta1〜Beta6**: ルーティングと返送モニター、録音、仮想音声デバイス、VST3、MIDI、映像の順に追加
3. **RC1、v1.0**: 総合的な確認を経て正式リリース

進み具合は GitHub の Milestones で確認できます。

## ビルドとテスト

### 必要なもの

- CMake 3.25 以上
- C++20 に対応したコンパイラ（Windows では Visual Studio 2022）
- Go 1.22 以上（ランデブーサーバー用）

アプリの対象は Windows 11 と Windows 10（x64）です。
OS に依存しない部分とランデブーサーバーは Linux でもビルドとテストができます。

### コマンド

```sh
# Release 構成で設定する
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# ビルドする
cmake --build build --config Release

# C++ のテストを実行する
ctest --test-dir build -C Release

# ランデブーサーバーのテストを実行する
cd infra/rendezvous && go test ./...
```

## GitHub での開発

作業は Issue から始め、PR でマージします。ルールの詳細は [AGENTS.md](AGENTS.md) の第 16 節と第 17 節にあります。

最初に一度だけ、リポジトリの設定で次を行ってください。

1. **Actions の「Repository setup」を実行する**: ラベルとマイルストーンが作られます。
2. **@claude を使う場合**: Settings → Secrets and variables → Actions に `CLAUDE_CODE_OAUTH_TOKEN`（Claude の契約を使う場合。手元で `claude setup-token` を実行して発行）か `ANTHROPIC_API_KEY` を登録し、[Claude GitHub App](https://github.com/apps/claude) をこのリポジトリにインストールします。
3. **main を保護する（推奨）**: Settings → Branches でルールを追加し、「PR を必須にする」と「CI の成功を必須にする」を有効にします。

## ライセンス

[GNU Affero General Public License v3.0](LICENSE) で公開しています。配布するバイナリには、対応するソースコードの入手先を示します。
