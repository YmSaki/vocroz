# 0010. 外部ライブラリはソースから固定バージョンで取得し、JSON には nlohmann/json を使う

- 状態: 採用（2026-11-07）

## コンテキスト

以降の実装は、JUCE（ADR-0005）、libopus（ADR-0002）、Noise の実装と libsodium（ADR-0001）を使う。
また、UI とエンジン間のコマンドとイベントを JSON でやり取りする（ADR-0003）が、JSON のライブラリは決まっていなかった。
取得方法を決めずに進めると、開発者ごとに手元のインストール（vcpkg、apt、手動のダウンロード）が混ざり、CI と手元でバージョンが食い違う。
また、JUCE は Linux でビルドするのに GTK と WebKitGTK を要求するが、Linux の CI はドメインの単体テストを回すためにあり、GUI のライブラリを入れたくない。

## 決定

- 外部ライブラリは CMake の FetchContent でソースから取得し、`cmake/<ライブラリ名>.cmake` に 1 ライブラリずつ書く。ルートの `CMakeLists.txt` がそれらを include する。
- バージョンは次のどちらかで固定する。
  - リリースの tar ボールがあるもの: URL と SHA-256（`URL_HASH`）。
  - Git でしか取得できないもの: コミットのハッシュ。履歴が大きい JUCE だけは、タグで浅く取得したあとにコミットのハッシュを検査する（`vocroz_verify_git_revision()`）。
- 取得するライブラリとバージョンは次のとおり。

| ライブラリ | バージョン | 取得元 | ターゲット |
| --- | --- | --- | --- |
| JUCE | 9.0.5 | Git のタグ（コミットを検査） | `juce::juce_*` |
| libopus | 1.5.2 | リリースの tar ボール | `Opus::opus` |
| libsodium | 1.0.20 | robinlinden/libsodium-cmake のコミット | `sodium` |
| noise-c | コミット `cfe2541` | Git のコミット | `noise::protocol` |
| nlohmann/json | 3.12.0 | リリースの `json.tar.xz` | `nlohmann_json::nlohmann_json` |
| WebView2 SDK | 1.0.3912.50 | NuGet のパッケージ（SHA-256 を検査、Windows だけ。`docs/adr/0020-ipc-pipe-and-webui-bridge.md`） | `juce::juce_webview2`（`vocroz::juce` 経由） |

- JSON には nlohmann/json を使う。使用してよい範囲は `control-protocol` と `persistence`（AGENTS.md 4.2）。信頼境界では例外を使わない `parse(..., nullptr, false)` で受け、`is_discarded()` で検査する。
- オプション `VOCROZ_WITH_JUCE`（Windows では既定で ON、それ以外は OFF）を用意する。OFF のときは JUCE を取得せず、JUCE を使うターゲット（アダプターと apps）を定義しない。JUCE を使うターゲットは `if(VOCROZ_WITH_JUCE)` の中で定義する。
- noise-c は autotools でしかビルドできないので、`libnoiseprotocol` と同じソースを `cmake/noise-c.cmake` で CMake のターゲットにする。暗号プリミティブは libsodium のバックエンドを使う。Curve448 と NewHope は MSVC でビルドできないので、生成に失敗する実装（`cmake/noise-c/unsupported_dh.c`）に差し替える。
- 各ライブラリには、ビルドとリンクと最小限の呼び出しを確かめるテストを `cmake/link-tests/` に置く。
- libopus は DRED と OSCE（深層学習による拡張）を無効にしてビルドする。

## 理由

- ソースから固定バージョンで取得すれば、Windows、Linux、CI、手元のどこでも同じコードがビルドされる。パッケージマネージャーを追加で入れる必要もない。
- ハッシュで固定すれば、取得元の改ざんやタグの付け替えに気づける。暗号ライブラリを含むので、この確認は省かない。
- libsodium と noise-c を同じソースからビルドすれば、noise-c の暗号処理を libsodium の実装にそろえられ、暗号プリミティブを自作しない方針（ADR-0001）とも合う。
- libsodium の公式のビルドは autotools と Visual Studio のプロジェクトだけで、CMake 用の定義を自作すると、プラットフォームごとのマクロの設定を保守し続けることになる。広く使われている CMake のラッパーを使えば、その保守を負わずに済む。
- nlohmann/json はヘッダーだけで使え、MIT ライセンスで、C++ の型との変換を宣言的に書ける。DTO とシリアライズだけを持つ `control-protocol` の用途に合う。速度は simdjson などに劣るが、IPC のメッセージは小さく、オーディオスレッドでは JSON を扱わないので、低遅延の要件には影響しない。
- Linux の CI で JUCE を外せば、GUI のライブラリなしでドメインの単体テストを回し続けられる。
- DRED と OSCE は低遅延モード（ADR-0002）では使わず、ビルド時間とバイナリのサイズが増えるだけになる。

## 結果

- 初回の configure でソースのダウンロードが入る。GoogleTest と同じく、オフライン環境では `FETCHCONTENT_SOURCE_DIR_<名前>` で手元のソースを指定する。
- Windows の CI は JUCE の取得と `juceaide` のビルドで時間が延びる。遅さが問題になったら、取得したソースのキャッシュを CI に追加する。
- ライブラリのバージョンを上げるときは、`cmake/<ライブラリ名>.cmake` のバージョンとハッシュ、この ADR の表、CONTRIBUTING.md のライセンス一覧を同じ PR で更新する。
- AGENTS.md 4.2 の外部ライブラリの表に nlohmann/json の行を、5.1 の配置表にリンク確認テストの行を追加する。
