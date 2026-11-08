// SPDX-License-Identifier: AGPL-3.0-only
//
// 更新の前に設定を退避し、ロールバックで戻すポート。実装は apps/updater/src/win/。
#pragma once

namespace vocroz::update::ports {

/// 設定のバックアップ。
class SettingsBackup {
public:
    virtual ~SettingsBackup() = default;

    /// 現在の設定を退避する。設定ファイルがまだなければ、何もせずに成功とする。
    /// @return 退避できたら true。コントロールスレッドから呼ぶ。
    [[nodiscard]] virtual bool backup() = 0;

    /// 退避した設定に戻す。
    /// @return 戻せたら true。
    [[nodiscard]] virtual bool restore() = 0;
};

}  // namespace vocroz::update::ports
