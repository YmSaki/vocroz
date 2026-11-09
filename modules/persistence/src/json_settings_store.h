// SPDX-License-Identifier: AGPL-3.0-only
//
// session の SettingsStore を、--data-dir の下の JSON ファイルで実装する。
// 保存は AtomicFile で行い、本体が壊れていればバックアップから読む。
// 決めたことは docs/adr/0027-persistence.md にある。
#pragma once

#include <filesystem>

#include "vocroz/common/ports/logger.h"
#include "vocroz/session/ports/settings_store.h"

#include "atomic_file.h"

namespace vocroz::persistence {

/// 親機と子機の設定を、モードごとの JSON ファイルに保存する。
/// スレッド安全性: コントロールスレッドからだけ呼ぶ（SettingsStore と同じ）。
class JsonSettingsStore final : public session::SettingsStore {
public:
    /// @param data_dir 設定を置くフォルダ（--data-dir）。なければ最初の保存で作る
    /// @param logger   バックアップからの復元などを記録する。ライフタイムはこのオブジェクトより長い
    JsonSettingsStore(const std::filesystem::path& data_dir, common::Logger& logger);

    common::Result<session::HostSettings, session::SettingsError> load_host() override;
    common::Result<void, session::SettingsError> save_host(
        const session::HostSettings& settings) override;
    common::Result<session::ClientSettings, session::SettingsError> load_client() override;
    common::Result<void, session::SettingsError> save_client(
        const session::ClientSettings& settings) override;

private:
    AtomicFile host_file_;
    AtomicFile client_file_;
    common::Logger& logger_;
};

}  // namespace vocroz::persistence
