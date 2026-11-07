# VOCROZ

VOCROZ は、離れた場所にいる演奏者どうしがインターネット越しに合奏するための Windows アプリです。
各出演者の音声を 1 台の操作卓（PA）に集め、オペレーターがリアルタイムにミキシングして出力します。

## 現状

開発の初期段階で、まだ使えるリリースはありません。
最初のリリース（Alpha）の内容と進み具合は、GitHub の [Milestones](https://github.com/YmSaki/vocroz/milestones) で確認できます。

## 使うための条件（予定）

- Windows 11 または Windows 10（x64）
- 出演者と操作卓のあいだが、インターネットで直接つながること。
  音声を中継するサーバーを使わないため、回線の組み合わせによってはつながりません。つながらない場合は、アプリが原因と対処法を表示します。
- 合奏に耐える遅延にするには、互いに近い地域にいて、安定した固定回線を使うことが前提になります。

## 開発に参加する

最初に [AGENTS.md](AGENTS.md) を読んでください。設計の方針、コードの置き場所、PR の出し方をまとめています。

ビルドには CMake 3.25 以上、C++20 に対応したコンパイラ（Windows では Visual Studio 2022）、Go 1.22 以上が必要です。
OS に依存しない部分とランデブーサーバーは Linux でもビルドできます。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release

cd infra/rendezvous && go test ./...
```

## ライセンス

[GNU Affero General Public License v3.0](LICENSE) で公開しています。配布するバイナリには、対応するソースコードの入手先を示します。
