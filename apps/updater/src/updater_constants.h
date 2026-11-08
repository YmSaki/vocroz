// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-updater だけで使う定数。終了コードは remote-pa が結果を判断する契約なので、
// docs/adr/0013-installer-and-updater.md の表と一致させる。
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace vocroz::updater {

/// 適用した、または更新がなかった。
inline constexpr int kExitOk = 0;
/// 致命的なエラー。
inline constexpr int kExitFatal = 1;
/// 引数の誤り（docs/adr/0007-launch-interface.md と同じ値）。
inline constexpr int kExitUsage = 2;
/// --check で新しい版をダウンロードし、検証した。
inline constexpr int kExitUpdateStaged = 10;
/// セッションの稼働中なので適用しなかった。
inline constexpr int kExitBlockedBySession = 20;
/// 検証に失敗し、パッケージを破棄した。
inline constexpr int kExitRejected = 21;
/// 起動確認などに失敗し、旧版に戻した。
inline constexpr int kExitRolledBack = 22;

/// 起動確認で --version
/// を確かめる実行ファイル（拡張子なし）。バージョン別フォルダに入るものすべて。
inline constexpr std::array<std::string_view, 3> kVersionedExecutables = {"remote-pa", "pa-engine",
                                                                          "pa-updater"};

/// 起動確認で 1 つの実行ファイルを待つ上限。--version は初期化をほとんどしないので
/// 通常は 1 秒もかからない。ウイルス対策ソフトの初回スキャンで遅れる分を見込んだ値。
inline constexpr std::chrono::milliseconds kHealthCheckTimeout{15000};

/// インストール先の起動スタブのファイル名。
inline constexpr std::string_view kLauncherFileName = "vocroz.exe";

/// データフォルダ内の、更新のステージング領域のフォルダ名。
inline constexpr std::string_view kUpdatesDirName = "updates";
/// ステージング領域のパッケージのファイル名。
inline constexpr std::string_view kStagedPackageFileName = "package.zip";
/// ステージング領域の、検証済みマニフェストのファイル名。
inline constexpr std::string_view kStagedManifestFileName = "manifest.txt";
/// 退避した設定のファイル名。
inline constexpr std::string_view kSettingsBackupFileName = "settings.backup.json";
/// 設定ファイルのファイル名（persistence と同じ）。
inline constexpr std::string_view kSettingsFileName = "settings.json";

/// マニフェストの大きさの上限。7 行の短いテキストなので 16 KiB で十分に余裕がある。
inline constexpr std::uint64_t kMaxManifestSize = 16 * 1024;

}  // namespace vocroz::updater
