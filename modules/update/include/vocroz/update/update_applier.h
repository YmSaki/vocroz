// SPDX-License-Identifier: AGPL-3.0-only
//
// 更新の適用（pa-updater --apply）の手順。判断と順序だけを持ち、I/O はすべてポート経由で行う。
// 手順と失敗時の扱いは docs/adr/0009-installer-and-updater.md。
#pragma once

#include <string_view>

#include "vocroz/update/manifest_verifier.h"
#include "vocroz/update/ports/code_signature_verifier.h"
#include "vocroz/update/ports/health_checker.h"
#include "vocroz/update/ports/install_layout.h"
#include "vocroz/update/ports/session_monitor.h"
#include "vocroz/update/ports/settings_backup.h"
#include "vocroz/update/ports/staged_package.h"
#include "vocroz/update/semantic_version.h"

namespace vocroz::update {

/// 適用の結果。
enum class ApplyOutcome {
    /// 新しい版に切り替えた。
    kApplied,
    /// セッションが稼働中なので何も変えなかった。
    kBlockedBySession,
    /// 利用者の明示的な操作がないので何も変えなかった。
    kNotRequested,
    /// 現在の版より新しくないので何も変えなかった。
    kNotNewer,
    /// マニフェストかパッケージの検証に失敗した。パッケージは破棄した。
    kRejected,
    /// 設定の退避に失敗したので何も変えなかった。
    kAborted,
    /// 展開、署名、起動確認、切り替えのいずれかに失敗し、旧版に戻した。
    kRolledBack,
};

/// UpdateApplier が使うポート。所有権はコンポジションルートが持ち、UpdateApplier より長く生きる。
struct UpdateApplierPorts {
    ports::SessionMonitor& session_monitor;
    ports::StagedPackage& package;
    ports::SettingsBackup& settings_backup;
    ports::InstallLayout& install_layout;
    ports::CodeSignatureVerifier& code_signature_verifier;
    ports::HealthChecker& health_checker;
};

/// 更新トランザクションを実行する。
class UpdateApplier {
public:
    /// @param verifier 埋め込みの公開鍵を持つマニフェストの検証器
    /// @param running_version 現在動いている pa-updater のバージョン（現行版）
    /// @param ports 使うポート。参照先は UpdateApplier より長く生きること
    UpdateApplier(ManifestVerifier verifier, SemanticVersion running_version,
                  UpdateApplierPorts ports);

    /// ダウンロード済みのマニフェストとパッケージを検証し、適用する。
    /// 適用の判断（本番中のブロック）を先に行い、どの段階で失敗しても旧版が起動できる状態を保つ。
    /// @param manifest_text ダウンロード済みのマニフェスト全体
    /// @param user_requested 利用者が「更新して再起動」を押したか
    /// @return 適用の結果。コントロールスレッドから呼ぶ（ブロッキング I/O を含む）。
    [[nodiscard]] ApplyOutcome apply(std::string_view manifest_text, bool user_requested);

private:
    [[nodiscard]] bool verify_package(const UpdateManifest& manifest);
    [[nodiscard]] bool verify_signature(const UpdateManifest& manifest);
    [[nodiscard]] bool switch_to(const SemanticVersion& version);
    void prune_old_versions(const SemanticVersion& current, const SemanticVersion& previous);
    void roll_back(const SemanticVersion& version);

    ManifestVerifier verifier_;
    SemanticVersion running_version_;
    UpdateApplierPorts ports_;
};

}  // namespace vocroz::update
