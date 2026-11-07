# update

更新に関する判断を持つドメインモジュールです。OS の API は呼ばず、I/O はすべてポート経由で行います。
設計は [docs/adr/0009-installer-and-updater.md](../../docs/adr/0009-installer-and-updater.md) にあります。

## 責務

- セマンティックバージョンのパースと比較（`semantic_version.h`）
- 署名付き更新マニフェストの検証（`manifest_verifier.h`）。Ed25519 と SHA-256 は libsodium
- 更新を適用してよいかの判断。セッションの稼働中は適用しない（`update_policy.h`）
- 更新の確認とダウンロードの検証（`update_checker.h`）
- 更新トランザクションの実行とロールバック（`update_applier.h`、遷移表は `src/state/`）
- 現行版のポインターファイルの形式とバージョン別フォルダの名前（`install_pointer.h`）
- 起動スタブが起動するバージョンの選択（`launch_selection.h`）
- セッションの稼働を伝える名前付きミューテックスの名前（`session_lock.h`）

## ポート（`include/vocroz/update/ports/`）

| ポート | 役割 | 実装 |
| --- | --- | --- |
| `StagedPackage` | ダウンロード済みパッケージの読み込み、展開、破棄 | `apps/updater/src/win/` |
| `InstallLayout` | バージョン別フォルダとポインターファイル | `apps/updater/src/win/` |
| `CodeSignatureVerifier` | Authenticode の検証 | `apps/updater/src/win/` |
| `HealthChecker` | 新しい版の起動確認 | `apps/updater/src/win/` |
| `SettingsBackup` | 設定の退避と復元 | `apps/updater/src/win/` |
| `SessionMonitor` | セッションの稼働の判定 | `apps/updater/src/win/` |
| `UpdateSource` | マニフェストとパッケージの取得（HTTPS） | `apps/updater/src/win/` |

## 依存してよいモジュール

`common` だけです（AGENTS.md 4.2）。外部ライブラリは libsodium を使います。
