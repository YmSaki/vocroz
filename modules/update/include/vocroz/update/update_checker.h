// SPDX-License-Identifier: AGPL-3.0-only
//
// 更新の確認（pa-updater --check）の手順。マニフェストの取得と検証、パッケージのダウンロードと
// 検証までを行い、インストール先は変えない。セッションの稼働中に実行してよい。
#pragma once

#include "vocroz/update/manifest_verifier.h"
#include "vocroz/update/ports/staged_package.h"
#include "vocroz/update/ports/update_source.h"
#include "vocroz/update/semantic_version.h"

namespace vocroz::update {

/// 確認の結果。
enum class CheckOutcome {
    /// 現在の版が最新。
    kUpToDate,
    /// 新しい版をダウンロードし、検証した。--apply で適用できる。
    kUpdateStaged,
    /// マニフェストかパッケージの検証に失敗した。パッケージは破棄した。
    kRejected,
    /// 通信に失敗した。次回の確認で再び試す。
    kUnavailable,
};

/// 更新を確認する。
class UpdateChecker {
public:
    /// @param verifier 埋め込みの公開鍵を持つマニフェストの検証器
    /// @param running_version 現在動いているバージョン
    /// @param source 取得先。参照先は UpdateChecker より長く生きること
    /// @param package ステージング領域のパッケージ。参照先は UpdateChecker より長く生きること
    UpdateChecker(ManifestVerifier verifier, SemanticVersion running_version,
                  ports::UpdateSource& source, ports::StagedPackage& package);

    /// 更新を確認し、新しい版があればダウンロードして検証する。
    /// @return 確認の結果。コントロールスレッドから呼ぶ（ブロッキング I/O を含む）。
    [[nodiscard]] CheckOutcome check();

private:
    ManifestVerifier verifier_;
    SemanticVersion running_version_;
    ports::UpdateSource& source_;
    ports::StagedPackage& package_;
};

}  // namespace vocroz::update
