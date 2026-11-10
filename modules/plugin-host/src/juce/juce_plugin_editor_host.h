// SPDX-License-Identifier: AGPL-3.0-only
//
// プラグインのエディターを JUCE の DocumentWindow で開く、session の PluginEditorHost の実装
// （docs/adr/0041-plugin-editor-and-failure-notice.md）。
// ウィンドウはエディターを表示するだけで、プラグインの処理（グラフのノード）には触れないので、
// 閉じても音声は続く。
#pragma once

#include <map>
#include <memory>
#include <string_view>

#include "vocroz/session/ports/plugin_editor_host.h"

namespace vocroz::plugin_host {

/// JUCE の DocumentWindow でエディターを開く PluginEditorHost。
/// make_vst3_plugin_loader() で読み込んだインスタンス（JucePluginInstance）だけを扱う。
/// スレッド安全性: すべて JUCE のメッセージスレッドから呼ぶ。破棄もそのスレッドで行う。
class JucePluginEditorHost final : public session::PluginEditorHost {
public:
    JucePluginEditorHost();
    /// 開いているウィンドウをすべて閉じる。
    ~JucePluginEditorHost() override;
    JucePluginEditorHost(const JucePluginEditorHost&) = delete;
    JucePluginEditorHost& operator=(const JucePluginEditorHost&) = delete;
    JucePluginEditorHost(JucePluginEditorHost&&) = delete;
    JucePluginEditorHost& operator=(JucePluginEditorHost&&) = delete;

    [[nodiscard]] common::Result<void, session::PluginEditorError> open(
        session::PluginInstance& instance, std::string_view title) override;
    void close(const session::PluginInstance& instance) override;
    [[nodiscard]] bool is_open(const session::PluginInstance& instance) const override;
    void close_all() override;

private:
    class EditorWindow;

    struct Entry {
        std::unique_ptr<EditorWindow> window;
        // オペレーターが閉じるボタンを押したら下ろす。ウィンドウは隠して、後で破棄する。
        bool open = true;
    };

    // インスタンスごとのウィンドウ。閉じるボタンのコールバックの中ではウィンドウ自身を
    // 破棄できないので、後で破棄する処理をメッセージスレッドに積む。その処理がホストより
    // 後に実行されてもよいよう、表は shared_ptr で持ち、処理には weak_ptr を渡す。
    using Windows = std::map<const session::PluginInstance*, Entry>;

    static void on_close_button(const std::weak_ptr<Windows>& weak,
                                const session::PluginInstance* key);

    std::shared_ptr<Windows> windows_;
};

}  // namespace vocroz::plugin_host
