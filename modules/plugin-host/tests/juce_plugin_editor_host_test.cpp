// SPDX-License-Identifier: AGPL-3.0-only
//
// JucePluginEditorHost の単体テスト。フェイクのプラグインでウィンドウを開閉し、次を検証する。
// - インスタンスごとに 1 つだけ開き、2 回目は同じウィンドウを前面に出す
// - 独自の画面を持たないプラグインは、パラメーターの一覧で代える
// - エディターを作る途中で例外を送出したプラグインでも、エンジンを落とさずに失敗を返す
// - ウィンドウを閉じても、プラグインの処理は続く
// - グラフが先にプラグインを手放しても、ウィンドウが閉じるまでプラグインを生かしておく

#include "juce/juce_plugin_editor_host.h"

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "juce/juce_plugin_instance.h"
#include "juce/juce_plugin_processor.h"

namespace vocroz::plugin_host {
namespace {

// 破棄されたことをテストから確かめるための印。
struct Lifetime {
    bool destroyed = false;
};

// 振る舞いを切り替えられるステレオのエフェクト。
class EditorPlugin final : public juce::AudioPluginInstance {
public:
    explicit EditorPlugin(std::shared_ptr<Lifetime> lifetime)
        : AudioPluginInstance(BusesProperties()
                                  .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          lifetime_(std::move(lifetime)) {}
    ~EditorPlugin() override {
        lifetime_->destroyed = true;
    }
    EditorPlugin(const EditorPlugin&) = delete;
    EditorPlugin& operator=(const EditorPlugin&) = delete;
    EditorPlugin(EditorPlugin&&) = delete;
    EditorPlugin& operator=(EditorPlugin&&) = delete;

    bool own_editor = true;
    bool throw_on_editor = false;
    int editors_created = 0;

    void fillInPluginDescription(juce::PluginDescription& description) const override {
        description.name = "EditorPlugin";
    }
    const juce::String getName() const override {
        return "EditorPlugin";
    }
    void prepareToPlay(double /*rate*/, int /*block*/) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/) override {
        buffer.applyGain(0.5F);
    }
    double getTailLengthSeconds() const override {
        return 0.0;
    }
    bool acceptsMidi() const override {
        return false;
    }
    bool producesMidi() const override {
        return false;
    }
    juce::AudioProcessorEditor* createEditor() override {
        if (throw_on_editor) {
            throw std::runtime_error("editor failure");
        }
        ++editors_created;
        return new juce::GenericAudioProcessorEditor(*this);  // JUCE の取り決めで所有権を渡す
    }
    bool hasEditor() const override {
        return own_editor || throw_on_editor;
    }
    int getNumPrograms() override {
        return 1;
    }
    int getCurrentProgram() override {
        return 0;
    }
    void setCurrentProgram(int /*index*/) override {}
    const juce::String getProgramName(int /*index*/) override {
        return {};
    }
    void changeProgramName(int /*index*/, const juce::String& /*name*/) override {}
    void getStateInformation(juce::MemoryBlock& /*data*/) override {}
    void setStateInformation(const void* /*data*/, int /*size*/) override {}

private:
    std::shared_ptr<Lifetime> lifetime_;
};

// JucePluginInstance ではないインスタンス（別の実装で読み込んだもの）。
class OtherInstance final : public session::PluginInstance {
public:
    [[nodiscard]] std::shared_ptr<audio_core::PluginProcessor> processor() const override {
        return nullptr;
    }
    [[nodiscard]] common::Result<std::vector<std::uint8_t>, session::PluginStateError> save_state()
        override {
        return std::vector<std::uint8_t>{};
    }
    [[nodiscard]] common::Result<void, session::PluginStateError> restore_state(
        std::span<const std::uint8_t> /*state*/) override {
        return {};
    }
};

// JUCE のウィンドウはメッセージスレッドで扱うので、テストのスレッドをメッセージスレッドにする。
class JucePluginEditorHostTest : public ::testing::Test {
protected:
    JucePluginEditorHostTest() {
        auto owned = std::make_unique<EditorPlugin>(lifetime);
        plugin = owned.get();
        plugin->prepareToPlay(48000.0, 64);
        instance = std::make_unique<JucePluginInstance>(
            std::make_shared<JucePluginProcessor>(std::move(owned)));
    }

    // 閉じるボタンの後の破棄は、メッセージスレッドに積まれた処理で行うので、それを実行する。
    // 積まれた処理は積んだ順に実行されるので、最後にループを止める処理を積んでから回す。
    // ループを一度止めた MessageManager は再開できないが、テストごとに作り直すので問題ない。
    static void run_pending_messages() {
        auto* manager = juce::MessageManager::getInstance();
        juce::MessageManager::callAsync([manager] { manager->stopDispatchLoop(); });
        manager->runDispatchLoop();
    }

    // 開いているエディターのウィンドウ（タイトルで探す）。
    static juce::DocumentWindow* find_window(const juce::String& title) {
        for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i) {
            auto* window =
                dynamic_cast<juce::DocumentWindow*>(juce::Desktop::getInstance().getComponent(i));
            if (window != nullptr && window->getName() == title && window->isVisible()) {
                return window;
            }
        }
        return nullptr;
    }

    juce::ScopedJuceInitialiser_GUI juce_;
    std::shared_ptr<Lifetime> lifetime = std::make_shared<Lifetime>();
    EditorPlugin* plugin = nullptr;
    std::unique_ptr<JucePluginInstance> instance;
    JucePluginEditorHost host;
};

TEST_F(JucePluginEditorHostTest, OpensOneWindowPerInstance) {
    ASSERT_TRUE(host.open(*instance, "EQ - Vocal").has_value());
    EXPECT_TRUE(host.is_open(*instance));
    EXPECT_NE(find_window("EQ - Vocal"), nullptr);

    // 2 回目は同じウィンドウを前面に出すだけで、エディターを作り直さない。
    ASSERT_TRUE(host.open(*instance, "EQ - Vocal").has_value());
    EXPECT_EQ(plugin->editors_created, 1);

    host.close(*instance);
    EXPECT_FALSE(host.is_open(*instance));
    EXPECT_EQ(find_window("EQ - Vocal"), nullptr);
}

TEST_F(JucePluginEditorHostTest, FallsBackToParameterListWithoutOwnEditor) {
    plugin->own_editor = false;
    ASSERT_TRUE(host.open(*instance, "Generic").has_value());
    EXPECT_TRUE(host.is_open(*instance));
    EXPECT_EQ(plugin->editors_created, 0);
}

TEST_F(JucePluginEditorHostTest, ReturnsAnErrorWhenThePluginThrows) {
    plugin->throw_on_editor = true;
    const auto opened = host.open(*instance, "Broken");
    ASSERT_FALSE(opened.has_value());
    EXPECT_EQ(opened.error(), session::PluginEditorError::kOpenFailed);
    EXPECT_FALSE(host.is_open(*instance));
}

TEST_F(JucePluginEditorHostTest, RejectsInstancesFromAnotherLoader) {
    OtherInstance other;
    const auto opened = host.open(other, "Other");
    ASSERT_FALSE(opened.has_value());
    EXPECT_EQ(opened.error(), session::PluginEditorError::kNotSupported);
}

TEST_F(JucePluginEditorHostTest, KeepsProcessingAfterTheOperatorClosesTheWindow) {
    const std::shared_ptr<audio_core::PluginProcessor> processor = instance->processor();
    ASSERT_TRUE(host.open(*instance, "Comp").has_value());
    juce::DocumentWindow* window = find_window("Comp");
    ASSERT_NE(window, nullptr);

    window->closeButtonPressed();
    EXPECT_FALSE(host.is_open(*instance));
    run_pending_messages();
    EXPECT_EQ(find_window("Comp"), nullptr);

    // ウィンドウを閉じてもプラグインは処理を続ける。
    std::vector<float> left(64, 1.0F);
    std::vector<float> right(64, 1.0F);
    float* channels[] = {left.data(), right.data()};
    audio_core::PluginProcessContext context;
    context.channels = std::span<float* const>(channels, 2);
    context.frames = 64;
    EXPECT_EQ(processor->process(context), audio_core::PluginProcessStatus::kOk);
    EXPECT_FLOAT_EQ(left[0], 0.5F);

    // 閉じた後でも、もう一度開ける。
    ASSERT_TRUE(host.open(*instance, "Comp").has_value());
    EXPECT_TRUE(host.is_open(*instance));
}

TEST_F(JucePluginEditorHostTest, KeepsThePluginAliveWhileTheWindowIsOpen) {
    ASSERT_TRUE(host.open(*instance, "Reverb").has_value());
    // 取り決め（インスタンスの破棄の前に close() を呼ぶ）が守られず、インスタンスとグラフの処理が
    // 先にプラグインを手放しても、エディターが参照している間は生かしておく。
    const session::PluginInstance* key = instance.get();
    instance.reset();
    EXPECT_FALSE(lifetime->destroyed);
    host.close(*key);  // close() は表のキーとしてアドレスを使うだけで、中身には触れない
    EXPECT_TRUE(lifetime->destroyed);
}

}  // namespace
}  // namespace vocroz::plugin_host
