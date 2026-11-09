// SPDX-License-Identifier: AGPL-3.0-only
//
// JsonSettingsStore の実装。親機と子機で読み込みと保存の手順は同じなので、
// 変換の関数を引数にとるテンプレートで 1 か所にまとめる。

#include "json_settings_store.h"

#include <string>

#include "persistence_constants.h"
#include "settings_codec.h"

namespace vocroz::persistence {
namespace {

using session::SettingsError;

// 本体 → バックアップの順に読み、最初にパースできたものを返す。
// バックアップから読めたら、本体が壊れていたことを記録する。設定にはミュートの状態を保存しないので
// （vocroz/session/settings.h）、バックアップから戻しても起動はミュートから始まり、いきなり音は出ない。
template <typename Settings, typename Decode>
common::Result<Settings, SettingsError> load_settings(const AtomicFile& file, Decode decode,
                                                      common::Logger& logger) {
    const auto current = file.read_current();
    if (current && current.value()) {
        if (auto settings = decode(*current.value())) {
            return std::move(*settings);
        }
    }
    const auto backup = file.read_backup();
    if (backup && backup.value()) {
        if (auto settings = decode(*backup.value())) {
            logger.log(common::LogLevel::kWarning,
                       "設定ファイルを読めなかったため、1 世代前のバックアップから復元しました");
            return std::move(*settings);
        }
    }
    const bool current_missing = current && !current.value();
    const bool backup_missing = backup && !backup.value();
    if (current_missing && backup_missing) {
        return common::make_unexpected(SettingsError::kNotFound);
    }
    const bool read_failed = (!current && current.error() == AtomicFileError::kIoError) ||
                             (!backup && backup.error() == AtomicFileError::kIoError);
    logger.log(common::LogLevel::kError,
               "設定ファイルとバックアップのどちらも読めないため、既定値を使います");
    return common::make_unexpected(read_failed ? SettingsError::kIoError
                                               : SettingsError::kCorrupted);
}

// 今の本体がパースできるときだけバックアップに回し、壊れた内容で正常なバックアップを上書きしない。
template <typename Decode>
common::Result<void, SettingsError> save_settings(AtomicFile& file, const std::string& encoded,
                                                  Decode decode, common::Logger& logger) {
    const auto current = file.read_current();
    const bool current_is_valid =
        current && current.value() && decode(*current.value()).has_value();
    if (!file.commit(encoded, current_is_valid)) {
        logger.log(common::LogLevel::kError, "設定ファイルを保存できませんでした");
        return common::make_unexpected(SettingsError::kIoError);
    }
    return {};
}

}  // namespace

JsonSettingsStore::JsonSettingsStore(const std::filesystem::path& data_dir, common::Logger& logger)
    : host_file_(data_dir / kHostSettingsFileName),
      client_file_(data_dir / kClientSettingsFileName),
      logger_(logger) {}

common::Result<session::HostSettings, SettingsError> JsonSettingsStore::load_host() {
    return load_settings<session::HostSettings>(host_file_, decode_host_settings, logger_);
}

common::Result<void, SettingsError> JsonSettingsStore::save_host(
    const session::HostSettings& settings) {
    return save_settings(host_file_, encode_host_settings(settings), decode_host_settings, logger_);
}

common::Result<session::ClientSettings, SettingsError> JsonSettingsStore::load_client() {
    return load_settings<session::ClientSettings>(client_file_, decode_client_settings, logger_);
}

common::Result<void, SettingsError> JsonSettingsStore::save_client(
    const session::ClientSettings& settings) {
    return save_settings(client_file_, encode_client_settings(settings), decode_client_settings,
                         logger_);
}

}  // namespace vocroz::persistence
