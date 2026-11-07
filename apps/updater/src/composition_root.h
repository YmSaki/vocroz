// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-updater のコンポジションルート。Windows のアダプターを生成し、update の
// UpdateChecker と UpdateApplier に注入して、コマンドを実行する。
#pragma once

#include <filesystem>
#include <string>

#include "launch_options.h"
#include "win/file_install_layout.h"
#include "win/file_settings_backup.h"
#include "win/mutex_session_monitor.h"
#include "win/process_health_checker.h"
#include "win/tar_staged_package.h"
#include "win/winhttp_update_source.h"
#include "win/wintrust_code_signature_verifier.h"

namespace vocroz::updater {

class CompositionRoot {
public:
    /// @param options 検証済みの起動オプション（コマンドは kCheck か kApply）
    /// @param install_dir 解決済みのインストール先
    /// @param data_dir 解決済みのデータフォルダ
    CompositionRoot(LaunchOptions options, std::filesystem::path install_dir,
                    std::filesystem::path data_dir);

    /// コマンドを実行する。
    /// @return プロセスの終了コード（updater_constants.h）。
    [[nodiscard]] int run();

private:
    [[nodiscard]] int run_check();
    [[nodiscard]] int run_apply();
    void restart_if_requested();

    LaunchOptions options_;
    std::filesystem::path install_dir_;
    std::filesystem::path data_dir_;

    // アダプター。ドメインのオブジェクトは run_check() と run_apply() の中で生成し、
    // これらへの参照を渡すので、アダプターのほうが長く生きる。
    win::FileInstallLayout install_layout_;
    win::TarStagedPackage package_;
    win::FileSettingsBackup settings_backup_;
    win::WinTrustCodeSignatureVerifier code_signature_verifier_;
    win::ProcessHealthChecker health_checker_;
    win::MutexSessionMonitor session_monitor_;
    win::WinHttpUpdateSource update_source_;
};

}  // namespace vocroz::updater
