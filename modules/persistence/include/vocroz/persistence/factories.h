// SPDX-License-Identifier: AGPL-3.0-only
//
// persistence のアダプターを作るファクトリー関数。具象クラスは公開せず、ポートの型で返す
// （AGENTS.md 5.1）。コンポジションルートだけが呼ぶ。
// 置き場所は --data-dir の下に固定する
// （docs/adr/0007-launch-interface.md、docs/adr/0027-persistence.md）。
//   <data-dir>/host-settings.json、client-settings.json（とその .bak）  設定
//   <data-dir>/plugin-catalog.json                                    プラグインのスキャンの記録
//   <data-dir>/logs/<base_name>.log                                   ログ
//   <data-dir>/secrets/*.bin                                          DPAPI で暗号化した秘密情報
#pragma once

#include <filesystem>
#include <memory>

#include "vocroz/common/ports/logger.h"
#include "vocroz/persistence/log_options.h"
#include "vocroz/session/ports/plugin_catalog_store.h"
#include "vocroz/session/ports/secret_store.h"
#include "vocroz/session/ports/settings_store.h"

namespace vocroz::persistence {

/// <data-dir>/logs を返す。LogOptions::directory と DiagnosticReportInput::log_dir に使う。
/// @param data_dir --data-dir
/// @return ログのフォルダ
[[nodiscard]] std::filesystem::path log_directory(const std::filesystem::path& data_dir);

/// 設定を data_dir の JSON ファイルに保存する SettingsStore を作る。
/// @param data_dir --data-dir。なければ最初の保存で作る
/// @param logger   バックアップからの復元などを記録する。ライフタイムは戻り値より長い
/// @return SettingsStore
[[nodiscard]] std::unique_ptr<session::SettingsStore> make_json_settings_store(
    const std::filesystem::path& data_dir, common::Logger& logger);

/// プラグインのスキャンの記録を data_dir の JSON ファイルに保存する PluginCatalogStore を作る。
/// @param data_dir --data-dir。なければ最初の保存で作る
/// @param logger   壊れた記録などを記録する。ライフタイムは戻り値より長い
/// @return PluginCatalogStore。スキャン専用のスレッドから使ってよい（PluginCatalog と同じスレッド）
[[nodiscard]] std::unique_ptr<session::PluginCatalogStore> make_json_plugin_catalog_store(
    const std::filesystem::path& data_dir, common::Logger& logger);

/// 大きさでローテーションするログファイルの Logger を作る。
/// フォルダを作れなかったりファイルを開けなかったりしても失敗にせず、ログを捨てる Logger として動く
/// （ログの失敗で起動を止めないため）。
/// @param options 出力先と上限
/// @return Logger
[[nodiscard]] std::unique_ptr<common::Logger> make_rotating_file_logger(LogOptions options);

#ifdef _WIN32
/// 秘密情報を <data-dir>/secrets に DPAPI で暗号化して保存する SecretStore を作る（Windows だけ）。
/// @param data_dir --data-dir
/// @return SecretStore
[[nodiscard]] std::unique_ptr<session::SecretStore> make_dpapi_secret_store(
    const std::filesystem::path& data_dir);
#endif

}  // namespace vocroz::persistence
