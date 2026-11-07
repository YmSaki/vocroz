# VOCROZ

インターネット越しに複数の出演者の音声を集め、1台の親機（PA）でミキシングして出力する Windows 向けデスクトップアプリです。合奏など低遅延が必要な用途を想定しています。

> 開発初期段階です。現在は Alpha の P0（基盤と技術検証）を進めています。

## 構成

| 実行ファイル | 役割 |
| --- | --- |
| `remote-pa` | 親機の操作卓 UI（WebView2）。子機モードでは出演者用クライアントとして動く |
| `pa-engine` | 親機の音声エンジン。セッション・通信・ミキシング・出力の状態をすべて保持する |
| `pa-updater` | セッション終了後にだけ起動し、署名を検証して更新する短命プロセス |
| `infra/rendezvous` | 招待解決・STUN・アドレス交換を行う Go 製サーバー。音声は通さない |

設計判断は [docs/adr](docs/adr/)、開発ルール（設計原則・デザインパターン・コーディング規則）は [AGENTS.md](AGENTS.md) にまとめています。

## ビルド

必要なもの: CMake 3.25 以上、C++20 コンパイラ（Windows では Visual Studio 2022）、Go 1.22 以上。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release

cd infra/rendezvous && go test ./...
```

## GitHub での開発

作業は Issue から始め、PR でマージします。ルールの詳細は [AGENTS.md](AGENTS.md) 第 16〜17 節にあります。

最初に一度だけ、リポジトリの設定で次を行ってください。

1. **Actions → Repository setup → Run workflow** を実行し、ラベルとマイルストーンを作る。
2. **@claude を使う場合**: Settings → Secrets and variables → Actions に `CLAUDE_CODE_OAUTH_TOKEN`（Claude の契約を使う場合。手元で `claude setup-token` を実行して発行）か `ANTHROPIC_API_KEY` を登録し、[Claude GitHub App](https://github.com/apps/claude) をこのリポジトリにインストールする。
3. **main の保護（推奨）**: Settings → Branches でルールを追加し、「PR を必須にする」「CI の成功を必須にする」を有効にする。

## ライセンス

[GNU Affero General Public License v3.0](LICENSE)。配布するバイナリには、対応するソースの入手先を示します。
