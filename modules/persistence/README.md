# persistence

設定、ログ、秘密情報をディスクに保存するアダプターのモジュールです。依存してよいのは `session` と `common` です（AGENTS.md 4.2）。
置き場所、保存の手順、伏せる規則は `docs/adr/0027-persistence.md` で、親機の設定に足したルーティングの欄の形は `docs/adr/0033-routing-persistence.md` で、SRTP 外部出力の欄と鍵の保存は `docs/adr/0040-srtp-external-output.md` で決めています。

## 責務

- `session::SettingsStore` の実装。親機と子機の設定を `--data-dir` の JSON ファイルにアトミックに保存し、1 世代のバックアップを残す。本体が壊れていればバックアップから読む
- `common::Logger` の実装。`<data-dir>/logs` のファイルに 1 件 1 行で書き、大きさでローテーションする（既定 10MB × 5）
- ログと診断レポートの秘密情報（鍵、トークン、招待コード）と個人情報（グローバルな IP アドレス、ユーザー名、セッション名、バスと出力の名前）の伏せ字
- マスキング済みの診断レポートの書き出し
- `session::SecretStore` の実装。秘密情報（静的秘密鍵、SRTP 外部出力の鍵）を DPAPI で暗号化して `<data-dir>/secrets` に保存する（Windows だけ）

設定値の意味づけ（既定値、値の範囲、起動時のミュート）は `session` が持ちます。

## 公開 API

| ヘッダー | 内容 |
| --- | --- |
| `vocroz/persistence/factories.h` | `make_json_settings_store()`、`make_rotating_file_logger()`、`make_dpapi_secret_store()`（Windows だけ）、`log_directory()` |
| `vocroz/persistence/log_options.h` | `LogOptions`（ログの出力先、ファイル名、重要度、上限） |
| `vocroz/persistence/diagnostic_report.h` | `write_diagnostic_report()`、`DiagnosticReportInput`、`DiagnosticReportError` |

## ソースの構成

| パス | 内容 |
| --- | --- |
| `src/atomic_file.{h,cpp}` | 一時ファイル → フラッシュ → 置き換えの保存と 1 世代のバックアップ |
| `src/settings_codec.{h,cpp}` | 設定ファイルの JSON の変換（信頼境界） |
| `src/json_settings_store.{h,cpp}` | `SettingsStore` の実装 |
| `src/rotating_file_logger.{h,cpp}` | `Logger` の実装 |
| `src/secret_masker.{h,cpp}` | 伏せ字の規則 |
| `src/secret_file_store.{h,cpp}`、`src/data_protector.h` | `SecretStore` の実装と、暗号化の境界 |
| `src/diagnostic_report.cpp` | 診断レポート |
| `src/detail/durable_io.h` | ディスクまで書き切る 2 つの操作。実装は `src/win/win32_durable_io.cpp` と `src/posix/posix_durable_io.cpp`（Linux の CI のテスト用） |
| `src/win/dpapi_data_protector.{h,cpp}` | DPAPI による暗号化 |

## スレッド

`SettingsStore`、`SecretStore`、診断レポートはコントロールスレッドから呼びます。ファイル I/O でブロックします。
`Logger::log()` はオーディオスレッド以外のどのスレッドからも呼べます（内部で排他します）。

## テスト

`tests/` の単体テストは、一時フォルダの実際のファイルで、書き込み途中の中断や壊れたファイルを再現します。
`json_settings_store_test.cpp` は、壊れた設定から `HostSession` と `ClientSession` を起動し、全ミュートで始まることを確かめます。
DPAPI のテスト（`dpapi_data_protector_test.cpp`）は Windows の CI でだけ動きます。
