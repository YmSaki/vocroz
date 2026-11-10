// SPDX-License-Identifier: AGPL-3.0-only
//
// persistence の中だけで使う定数。ファイル名と大きさの上限を 1 か所にまとめる。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace vocroz::persistence {

/// 親機の設定ファイルの名前。親機モードと子機モードは同じ --data-dir で同時に動けるため
/// （docs/adr/0007-launch-interface.md の単一インスタンスの範囲）、2 つのプロセスが同じファイルを
/// 書き合わないよう、モードごとにファイルを分ける。
inline constexpr std::string_view kHostSettingsFileName = "host-settings.json";

/// 子機の設定ファイルの名前。
inline constexpr std::string_view kClientSettingsFileName = "client-settings.json";

/// プラグインのスキャンの記録のファイルの名前。親機だけが使う。設定とは保存の頻度と壊れたときの
/// 扱いが違うので、別のファイルにする（docs/adr/0032-plugin-catalog-and-loader.md）。
inline constexpr std::string_view kPluginCatalogFileName = "plugin-catalog.json";

/// スキャンの記録のファイルの形式のバージョン。
inline constexpr std::int64_t kPluginCatalogSchemaVersion = 1;

/// 秘密情報を置くサブフォルダの名前（--data-dir の下）。
inline constexpr std::string_view kSecretsDirectoryName = "secrets";

/// ログを置くサブフォルダの名前（--data-dir の下）。
inline constexpr std::string_view kLogsDirectoryName = "logs";

/// 書き込み途中の一時ファイルに付ける拡張子。
inline constexpr std::string_view kTemporarySuffix = ".tmp";

/// 1 世代前のバックアップに付ける拡張子。
inline constexpr std::string_view kBackupSuffix = ".bak";

/// 設定ファイルの形式のバージョン。形式を変えたら上げ、古い形式の読み替えをデコーダーに足す。
inline constexpr std::int64_t kSettingsSchemaVersion = 1;

/// 読み込む設定ファイルと秘密情報のファイルの大きさの上限（バイト）。実際の大きさは数百バイトなので、
/// 壊れて巨大になったファイルや別のファイルを丸ごとメモリに読まないための上限として 1 MiB にする。
inline constexpr std::uintmax_t kMaxSmallFileBytes = std::uintmax_t{1024} * 1024;

/// 診断レポートに入れるログの合計の上限（バイト）。メールの添付の上限（多くは 10MB 前後）に
/// 収まるよう、新しいログから 8 MiB までを入れる。
inline constexpr std::uintmax_t kMaxReportLogBytes = std::uintmax_t{8} * 1024 * 1024;

}  // namespace vocroz::persistence
