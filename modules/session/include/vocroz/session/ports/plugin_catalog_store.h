// SPDX-License-Identifier: AGPL-3.0-only
//
// プラグインのスキャンの記録を保存するポート。
// 設定（SettingsStore）とは別のポートにする（docs/adr/0032-plugin-catalog-and-loader.md）。
// 記録はプラグインの数だけ大きくなり、スキャンのたびに書き換わる。
// 壊れても再スキャンで作り直せる。
// 設定とは、保存の頻度と壊れたときの扱いが違うため。
#pragma once

#include <span>
#include <vector>

#include "vocroz/common/result.h"
#include "vocroz/session/plugin_scan_record.h"
#include "vocroz/session/settings.h"

namespace vocroz::session {

/// スキャンの記録の保存先。
/// スレッド安全性: PluginCatalog と同じスレッドからだけ呼ぶ。
/// 実装はファイル I/O でブロックしてよい。
class PluginCatalogStore {
public:
    virtual ~PluginCatalogStore() = default;

    /// 保存した記録を読む。
    /// @return 記録。読めなければ理由。呼び出し側は記録なしとして、すべてを再スキャンする
    [[nodiscard]] virtual common::Result<std::vector<PluginScanRecord>, SettingsError> load() = 0;

    /// 記録を保存する。途中で強制終了しても、前回の記録か今回の記録のどちらかが残る。
    /// @param records 保存する記録
    /// @return 失敗したら理由。呼び出し側は続行し、次の起動で再スキャンする
    [[nodiscard]] virtual common::Result<void, SettingsError> save(
        std::span<const PluginScanRecord> records) = 0;
};

}  // namespace vocroz::session
