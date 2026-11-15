// SPDX-License-Identifier: AGPL-3.0-only
//
// 挿入のプラグインを読み込み、破棄し、エディターを開き、状態を取るポート。
// 読み込みは JUCE のメッセージスレッドで行い、時間がかかるので、session は依頼だけをして結果を
// 後で HostSession::on_plugin_loaded() で受け取る。session はスレッドを知らずに済み、テストでは
// 読み込みの成功、失敗、遅れをフェイクで再現できる（docs/adr/0042-plugin-insert.md）。
// pa-engine が PluginLoader と PluginEditorHost を使って実装する。
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "vocroz/session/plugin_description.h"

namespace vocroz::session {

/// 読み込みの依頼 1 回ごとの番号。同じ挿入を外して挿し直したとき、前の依頼の遅れて届いた結果を
/// 見分けるために使う。
using PluginLoadTicket = std::uint64_t;

/// 挿入のプラグインの読み込みと破棄。
/// スレッド安全性: コントロールスレッドから呼ぶ。実装はブロックせず、依頼を別のスレッドへ渡す。
class PluginRack {
public:
    virtual ~PluginRack() = default;

    /// プラグインの読み込みを始める。結果は、成功でも失敗でも、コントロールスレッドで
    /// HostSession::on_plugin_loaded() に同じ insert_id と ticket で 1 回だけ届ける。
    /// 同じ insert_id で読み込んだものがあれば、先に release() されていること。
    /// state が空でなければ、読み込んだ直後、結果を届ける前に restore_state() で戻す。戻せなければ
    /// プラグインの既定の状態のまま使い、警告をログに残す（読み込みは成功として届ける）。
    /// @param insert_id   挿入の entity_id
    /// @param ticket      依頼の番号
    /// @param description プラグインの一覧にある記述
    /// @param state       保存してあったこのプラグインの状態。なければ空
    virtual void load(const std::string& insert_id, PluginLoadTicket ticket,
                      const PluginDescription& description,
                      std::span<const std::uint8_t> state) = 0;

    /// 挿入のプラグインの状態を取る。結果は、取れたものだけをまとめて、コントロールスレッドで
    /// HostSession::on_plugin_states_captured() に 1 回届ける。読み込んでいない挿入と、状態を
    /// 取れなかった挿入（警告をログに残す）は結果に入れない。
    /// @param insert_ids 状態を取る挿入の entity_id
    virtual void capture_states(std::vector<std::string> insert_ids) = 0;

    /// 挿入のプラグインを手放す。エディターを閉じ、インスタンスを破棄する。
    /// 読み込み中なら、読み込みが済んでから破棄する（結果は届けても届けなくてもよい）。
    /// グラフが処理をまだ持っていても、処理はグラフが手放すまで生き続ける。
    /// @param insert_id 挿入の entity_id。読み込んでいなければ何もしない
    virtual void release(const std::string& insert_id) = 0;

    /// 挿入のプラグインのエディターを開く。開いていれば前面に出す。
    /// 開けなければ、コントロールスレッドで HostSession::on_plugin_editor_failed() に届ける。
    /// 読み込みの済んだ挿入だけに呼ぶ（docs/adr/0041-plugin-editor-and-failure-notice.md）。
    /// @param insert_id 挿入の entity_id。読み込んでいなければ何もしない
    /// @param title     ウィンドウのタイトル
    virtual void open_editor(const std::string& insert_id, const std::string& title) = 0;
};

}  // namespace vocroz::session
