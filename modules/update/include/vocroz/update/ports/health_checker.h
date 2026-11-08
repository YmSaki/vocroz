// SPDX-License-Identifier: AGPL-3.0-only
//
// 新しい版の起動確認のポート。実装は apps/updater/src/win/（各実行ファイルを --version
// で起動する）。
#pragma once

#include "vocroz/update/semantic_version.h"

namespace vocroz::update::ports {

/// 起動確認。
class HealthChecker {
public:
    virtual ~HealthChecker() = default;

    /// そのバージョンのフォルダの実行ファイルが起動し、期待するバージョンを返すか確かめる。
    /// @return すべての実行ファイルが時間内に正常終了し、期待する出力を返したら true。
    ///         コントロールスレッドから呼ぶ。
    [[nodiscard]] virtual bool check(const SemanticVersion& version) = 0;
};

}  // namespace vocroz::update::ports
