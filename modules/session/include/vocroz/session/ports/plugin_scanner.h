// SPDX-License-Identifier: AGPL-3.0-only
//
// プラグインファイルを探してスキャンするポート。
// plugin-host が、ファイルごとに起動するスキャン用ヘルパー（pa-plugin-scanner）で実装する。
// どのファイルを再スキャンし、どれを読み込み不可とするかは、session の PluginCatalog が決める。
#pragma once

#include <vector>

#include "vocroz/common/result.h"
#include "vocroz/session/plugin_description.h"
#include "vocroz/session/plugin_scan_record.h"

namespace vocroz::session {

/// プラグインファイルの列挙とスキャン。
/// スレッド安全性: PluginCatalog と同じスレッドからだけ呼ぶ。実装はブロックしてよい。
class PluginScanner {
public:
    virtual ~PluginScanner() = default;

    /// 探す場所にあるプラグインファイルを列挙する。
    /// @return 見つけたファイル。読めない場所は飛ばす
    [[nodiscard]] virtual std::vector<PluginFile> list_plugin_files() = 0;

    /// ファイル 1 つをスキャンする。
    /// 不良プラグインが落ちても止まっても、呼び出し側は巻き込まれない。
    /// @param file list_plugin_files() が返したファイル
    /// @return 見つけたプラグイン（プラグインを含まなければ空）。失敗なら理由
    [[nodiscard]] virtual common::Result<std::vector<PluginDescription>, PluginScanFailure> scan(
        const PluginFile& file) = 0;
};

}  // namespace vocroz::session
