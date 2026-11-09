// SPDX-License-Identifier: AGPL-3.0-only
//
// JucePluginEditorHost（juce_plugin_editor_host.h）の実装。

#include "juce_plugin_editor_host.h"

#include <functional>
#include <utility>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "vocroz/plugin_host/factories.h"

#include "juce_plugin_instance.h"
#include "juce_plugin_processor.h"

namespace vocroz::plugin_host {

using common::make_unexpected;
using session::PluginEditorError;

/// エディター 1 つを表示するウィンドウ。閉じるボタンの扱いはホストに任せる。
class JucePluginEditorHost::EditorWindow final : public juce::DocumentWindow {
public:
    /// @param title ウィンドウのタイトル（UTF-8）
    /// @param processor エディターが参照するプラグイン
    /// @param editor 表示するエディター
    /// @param on_close 閉じるボタンが押されたときの処理
    EditorWindow(std::string_view title, std::shared_ptr<JucePluginProcessor> processor,
                 std::unique_ptr<juce::AudioProcessorEditor> editor, std::function<void()> on_close)
        : juce::DocumentWindow(
              juce::String::fromUTF8(title.data(), static_cast<int>(title.size())),
              juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                  juce::ResizableWindow::backgroundColourId),
              juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          processor_(std::move(processor)),
          on_close_(std::move(on_close)) {
        setUsingNativeTitleBar(true);
        // 大きさはエディターが決める。エディターが大きさを変えたら、ウィンドウも合わせる。
        setResizable(editor->isResizable(), false);
        setContentOwned(editor.release(), true);
    }

    EditorWindow(const EditorWindow&) = delete;
    EditorWindow& operator=(const EditorWindow&) = delete;
    EditorWindow(EditorWindow&&) = delete;
    EditorWindow& operator=(EditorWindow&&) = delete;

    ~EditorWindow() override {
        // メンバーは基底クラスより先に破棄されるので、エディターを先にここで破棄する。
        // エディターはプラグインを参照しているため、processor_ を手放す前に消す必要がある。
        clearContentComponent();
    }

    void closeButtonPressed() override {
        on_close_();
    }

private:
    // グラフのノードが先にプラグインを手放しても、エディターが参照している間は生かしておく。
    std::shared_ptr<JucePluginProcessor> processor_;
    std::function<void()> on_close_;
};

JucePluginEditorHost::JucePluginEditorHost() : windows_(std::make_shared<Windows>()) {}

JucePluginEditorHost::~JucePluginEditorHost() {
    close_all();
}

common::Result<void, PluginEditorError> JucePluginEditorHost::open(
    session::PluginInstance& instance, std::string_view title) {
    JUCE_ASSERT_MESSAGE_THREAD
    auto* juce_instance = dynamic_cast<JucePluginInstance*>(&instance);
    if (juce_instance == nullptr) {
        return make_unexpected(PluginEditorError::kNotSupported);
    }
    const auto found = windows_->find(&instance);
    if (found != windows_->end()) {
        if (found->second.open) {
            found->second.window->toFront(true);
            return {};
        }
        // オペレーターが閉じて、まだ破棄していないウィンドウ。作り直す。
        windows_->erase(found);
    }

    const std::shared_ptr<JucePluginProcessor>& processor = juce_instance->juce_processor();
    juce::AudioPluginInstance& plugin = processor->instance();
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    // 他社のプラグインのコードを呼ぶので、例外を送出されてもエンジンを落とさない。
    try {
        if (plugin.hasEditor()) {
            editor.reset(plugin.createEditorAndMakeActive());
        }
        // 独自の画面を持たない、または作れなかったプラグインは、パラメーターの一覧で代える。
        // 画面が開かないより、パラメーターだけでも操作できる方がオペレーターにとってよいため。
        if (!editor) {
            editor = std::make_unique<juce::GenericAudioProcessorEditor>(plugin);
        }
    } catch (...) {
        return make_unexpected(PluginEditorError::kOpenFailed);
    }

    const std::weak_ptr<Windows> weak = windows_;
    const session::PluginInstance* key = &instance;
    auto window = std::make_unique<EditorWindow>(title, processor, std::move(editor),
                                                 [weak, key] { on_close_button(weak, key); });
    window->centreWithSize(window->getWidth(), window->getHeight());
    window->setVisible(true);
    window->toFront(true);
    windows_->emplace(key, Entry{std::move(window), true});
    return {};
}

void JucePluginEditorHost::close(const session::PluginInstance& instance) {
    JUCE_ASSERT_MESSAGE_THREAD
    // インスタンスの破棄の前に呼ばれるので、オペレーターが閉じて破棄を待っているウィンドウも
    // ここで破棄する。エディターはインスタンスを参照しているため。
    windows_->erase(&instance);
}

bool JucePluginEditorHost::is_open(const session::PluginInstance& instance) const {
    JUCE_ASSERT_MESSAGE_THREAD
    const auto found = windows_->find(&instance);
    return found != windows_->end() && found->second.open;
}

void JucePluginEditorHost::close_all() {
    JUCE_ASSERT_MESSAGE_THREAD
    windows_->clear();
}

void JucePluginEditorHost::on_close_button(const std::weak_ptr<Windows>& weak,
                                           const session::PluginInstance* key) {
    const auto windows = weak.lock();
    if (!windows) {
        return;
    }
    const auto found = windows->find(key);
    if (found == windows->end()) {
        return;
    }
    found->second.open = false;
    found->second.window->setVisible(false);
    // 閉じるボタンのコールバックはウィンドウの中から呼ばれるので、ここでは破棄できない。
    // メッセージスレッドに積み、コールバックから戻った後で、閉じたままのウィンドウを破棄する。
    juce::MessageManager::callAsync([weak] {
        if (const auto alive = weak.lock()) {
            std::erase_if(*alive, [](const auto& item) { return !item.second.open; });
        }
    });
}

std::unique_ptr<session::PluginEditorHost> make_juce_plugin_editor_host() {
    return std::make_unique<JucePluginEditorHost>();
}

}  // namespace vocroz::plugin_host
