// SPDX-License-Identifier: AGPL-3.0-only
//
// UI に見せるプラグインの状態（スキャン中か、読み込み不可のファイル、プラグインの一覧の世代と
// ページ、挿入ごとの読み込みの状態）を組み立てる（docs/adr/0042-plugin-insert.md）。
// プラグインの一覧は数百件になり、1 メッセージの上限（64 KiB）を超えうるので、スナップショットには
// 入れず、世代を付けたページに分けて送る。各項目が control-protocol の上限に収まることもここで
// 確かめ、収まらないものは一覧に載せない（UI のデコーダーがページごと捨てないようにするため）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "vocroz/control_protocol/entities.h"
#include "vocroz/control_protocol/events.h"
#include "vocroz/session/plugin_description.h"
#include "vocroz/session/plugin_scan_record.h"

#include "plugin_insert_book.h"

namespace vocroz::session {

/// プラグインの状態（"plugins"）の revision。どのコマンドでも変わる値がない（スキャンの状態と
/// 挿入の状態は観測値）ので、進めずに 1 のままにする。
inline constexpr control_protocol::Revision kPluginsRevision = 1;

/// UI に見せるプラグインの状態。
/// スレッド安全性: コントロールスレッドからだけ使う。
class PluginStatusBook {
public:
    /// 一覧を置き換えたときに、載せなかったものの数。
    struct Dropped {
        std::size_t plugins = 0;  ///< 識別子か文字列が長すぎて一覧に載せなかったプラグイン
        std::size_t blocked = 0;  ///< 上限を超えたか、パスが長すぎて載せなかったファイル
    };

    /// プラグインの一覧と読み込み不可のファイルを置き換え、一覧の世代を進める。
    /// @param plugins 読み込めるプラグイン
    /// @param blocked 読み込み不可のファイルの記録
    /// @return 載せなかったものの数
    Dropped set_catalog(std::span<const PluginDescription> plugins,
                        std::span<const PluginScanRecord> blocked);

    /// スキャン中かどうかを記録する。
    /// @return 変わったら true
    bool set_scanning(bool scanning) noexcept;

    /// UI に送るプラグインの状態を返す。
    /// @param inserts 適用中の構成の挿入
    /// @param book    挿入ごとの読み込みの状態
    [[nodiscard]] control_protocol::PluginsInfo info(
        std::span<const control_protocol::RoutingInsert> inserts,
        const PluginInsertBook& book) const;

    /// プラグインの一覧を、今の世代のページに分けて返す。一覧が空でも 1 ページも返さない。
    [[nodiscard]] std::vector<control_protocol::PluginCatalogPage> pages() const;

private:
    bool scanning_ = false;
    std::uint64_t generation_ = 0;
    std::vector<control_protocol::PluginEntry> catalog_;
    std::vector<control_protocol::BlockedPluginFile> blocked_;
};

}  // namespace vocroz::session
