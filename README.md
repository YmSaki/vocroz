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

設計判断は [docs/adr](docs/adr/) にまとめています。

## ビルド

必要なもの: CMake 3.25 以上、C++20 コンパイラ（Windows では Visual Studio 2022）、Go 1.22 以上。

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release

cd infra/rendezvous && go test ./...
```

## ライセンス

[GNU Affero General Public License v3.0](LICENSE)。配布するバイナリには、対応するソースの入手先を示します。
