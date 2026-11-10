// SPDX-License-Identifier: AGPL-3.0-only
//
// 挿入ごとのプラグインの読み込みの状態（読み込み中、使える、失敗）と、グラフに渡す処理と失敗の印。
// 適用中のルーティングの挿入に合わせて、読み込みと破棄を PluginRack に依頼する。
// ミキサーへの反映と UI への通知は呼び出し側（HostContext）が行い、ここは状態だけを持つ
// （docs/adr/0042-plugin-insert.md）。
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "vocroz/audio_core/plugin_fault.h"
#include "vocroz/audio_core/ports/plugin_processor.h"
#include "vocroz/common/result.h"
#include "vocroz/control_protocol/entities.h"
#include "vocroz/session/plugin_description.h"
#include "vocroz/session/ports/mixer_control.h"
#include "vocroz/session/ports/plugin_loader.h"
#include "vocroz/session/ports/plugin_rack.h"

namespace vocroz::session {

/// 挿入の読み込みの状態。
enum class InsertLoadState {
    kLoading,  ///< 読み込みを依頼して、結果を待っている
    kReady,    ///< 読み込みが済み、グラフで使っている
    /// 読み込みか処理に失敗した。経路は無音のままにし、挿入を外して挿し直すまで戻さない
    kFailed,
};

/// 挿入が失敗した理由。
enum class InsertFailure {
    kNotFound,           ///< プラグインが見つからない
    kInstantiateFailed,  ///< インスタンスを作れなかった
    kUnsupportedLayout,  ///< チャンネル構成が合わない
    kProcessingFailed,   ///< 処理中に失敗した（PluginFault）
};

/// 挿入 1 つの状態。
struct InsertSlot {
    std::string plugin_id;
    PluginLoadTicket ticket = 0;
    InsertLoadState state = InsertLoadState::kLoading;
    std::optional<InsertFailure> failure;
    std::shared_ptr<audio_core::PluginProcessor> processor;
    std::shared_ptr<audio_core::PluginFault> fault;
};

/// 読み込みの結果。
using PluginLoadResult =
    common::Result<std::shared_ptr<audio_core::PluginProcessor>, PluginLoadError>;

/// 挿入ごとの読み込みの状態。
/// スレッド安全性: コントロールスレッドからだけ使う。
class PluginInsertBook {
public:
    /// @param rack 読み込みと破棄の依頼先。このオブジェクトより長く生きること
    explicit PluginInsertBook(PluginRack& rack);

    /// 適用中の構成の挿入に合わせる。新しい挿入と、プラグインを替えた挿入は読み込みを依頼し、
    /// なくなった挿入とプラグインを替えた挿入の前のインスタンスは破棄を依頼する。
    /// 構成を適用してから呼ぶ。グラフが前の処理を手放してから破棄されるようにするため。
    /// @param inserts 適用中の構成の挿入
    /// @param plugins 使えるプラグインの一覧。一覧にないプラグインの挿入は kNotFound で失敗にする
    void reconcile(std::span<const control_protocol::RoutingInsert> inserts,
                   std::span<const PluginDescription> plugins);

    /// 読み込みの結果を受け取る。今の依頼の結果でなければ（外したか挿し直した挿入の、遅れて
    /// 届いた結果）捨てる。
    /// @return 今の依頼の結果を反映したら true。読み込みが済んだらグラフを作り直す
    bool on_loaded(const std::string& insert_id, PluginLoadTicket ticket, PluginLoadResult result);

    /// 処理中に失敗した挿入を kFailed にする。グラフは失敗の印で既に無音にしているので、
    /// ここでは状態だけを変える。
    /// @return 新しく失敗にした挿入の entity_id
    std::vector<std::string> collect_faults();

    /// 構成の挿入のうち、読み込みの済んだもの。entity_id とプラグインが構成と同じものだけを返す。
    /// @param inserts グラフにする構成の挿入
    [[nodiscard]] std::vector<InsertedPlugin> ready_for(
        std::span<const control_protocol::RoutingInsert> inserts) const;

    /// 挿入の状態を返す。なければ nullptr。
    [[nodiscard]] const InsertSlot* find(const std::string& insert_id) const;

private:
    PluginRack& rack_;
    std::map<std::string, InsertSlot, std::less<>> slots_;
    PluginLoadTicket next_ticket_ = 1;
};

}  // namespace vocroz::session
