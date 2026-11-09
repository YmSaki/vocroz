// SPDX-License-Identifier: AGPL-3.0-only
//
// プラグインの一覧。
// スキャンの記録を、変更のないファイルを再スキャンしないためのキャッシュとして使う。
// スキャン中に落ちたり止まったりしたファイルは、読み込み不可として以降のスキャンから外す。
// 規則は docs/adr/0032-plugin-catalog-and-loader.md で決めた。
#pragma once

#include <cstddef>
#include <deque>
#include <vector>

#include "vocroz/session/plugin_description.h"
#include "vocroz/session/plugin_scan_record.h"
#include "vocroz/session/ports/plugin_catalog_store.h"
#include "vocroz/session/ports/plugin_scanner.h"

namespace vocroz::session {

/// scan_next() の結果。
enum class PluginScanProgress {
    kMore,  ///< まだスキャンするファイルが残っている
    kDone,  ///< 残りはない。記録を保存した
    /// スキャン用ヘルパーを起動できず、スキャンを中断した。記録は中断前のまま
    kScannerUnavailable,
};

/// プラグインの一覧とスキャンの進行。
/// スキャンはファイル 1 つずつ進め、呼び出し側が scan_next() を繰り返し呼ぶ。
/// ヘルパーの起動はファイルごとに時間がかかり、まとめて行うと呼び出し側のスレッドを長く止めるため。
/// スレッド安全性: すべて同じスレッド（コントロールスレッドか、スキャン専用のスレッド）から呼ぶ。
class PluginCatalog {
public:
    /// @param scanner ファイルの列挙とスキャン。このオブジェクトより長く生きること
    /// @param store 記録の保存先。このオブジェクトより長く生きること
    PluginCatalog(PluginScanner& scanner, PluginCatalogStore& store);

    /// 保存した記録を読む。読めなければ記録なしから始め、次の begin_rescan()
    /// ですべてをスキャンする。
    void load();

    /// スキャンを始める。
    /// ファイルを列挙し、記録がないか、更新日時かサイズが記録と違うファイルをスキャンの対象にする。
    /// 記録と同じファイルは、読み込み不可のものも含めてスキャンしない。
    /// なくなったファイルの記録は消す。
    /// @return スキャンするファイルの数
    std::size_t begin_rescan();

    /// スキャンの対象のファイルを 1 つスキャンする。対象がなくなったら記録を保存する。
    /// @return 進行の状況
    PluginScanProgress scan_next();

    /// @return スキャンの対象が残っていれば true
    [[nodiscard]] bool scanning() const noexcept;

    /// @return ファイルごとの記録（パスの順）
    [[nodiscard]] const std::vector<PluginScanRecord>& records() const noexcept;

    /// @return 読み込めるプラグイン（パスの順。読み込み不可のファイルのものは含まない）
    [[nodiscard]] std::vector<PluginDescription> available_plugins() const;

private:
    void put_record(PluginScanRecord record);

    PluginScanner& scanner_;
    PluginCatalogStore& store_;
    std::vector<PluginScanRecord> records_;
    std::deque<PluginFile> pending_;
};

}  // namespace vocroz::session
