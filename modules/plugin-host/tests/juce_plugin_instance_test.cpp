// SPDX-License-Identifier: AGPL-3.0-only
//
// JucePluginInstance の単体テスト。状態を持つフェイクのプラグインで、状態の保存と復元の往復と、
// プラグインが例外を送出したときに失敗を返してエンジンを落とさないことを検証する。

#include "juce/juce_plugin_instance.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "juce/juce_plugin_processor.h"

namespace vocroz::plugin_host {
namespace {

// 状態として 1 つの値（gain）を持つステレオのエフェクト。
class StatefulInstance final : public juce::AudioPluginInstance {
public:
    StatefulInstance()
        : AudioPluginInstance(BusesProperties()
                                  .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

    float gain = 1.0F;
    bool throw_on_state = false;

    void fillInPluginDescription(juce::PluginDescription& description) const override {
        description.name = "Stateful";
    }
    const juce::String getName() const override {
        return "Stateful";
    }
    void prepareToPlay(double /*rate*/, int /*block*/) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/) override {
        buffer.applyGain(gain);
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
        return nullptr;
    }
    bool hasEditor() const override {
        return false;
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
    void getStateInformation(juce::MemoryBlock& data) override {
        if (throw_on_state) {
            throw std::runtime_error("state failure");
        }
        data.replaceAll(&gain, sizeof(gain));
    }
    void setStateInformation(const void* data, int size) override {
        if (throw_on_state || size != static_cast<int>(sizeof(gain))) {
            throw std::runtime_error("state failure");
        }
        std::memcpy(&gain, data, sizeof(gain));
    }
};

struct Fixture {
    Fixture() {
        auto owned = std::make_unique<StatefulInstance>();
        plugin = owned.get();
        instance = std::make_unique<JucePluginInstance>(
            std::make_shared<JucePluginProcessor>(std::move(owned)));
    }

    StatefulInstance* plugin = nullptr;
    std::unique_ptr<JucePluginInstance> instance;
};

TEST(JucePluginInstanceTest, RestoresTheSavedState) {
    Fixture fixture;
    fixture.plugin->gain = 0.25F;
    auto saved = fixture.instance->save_state();
    ASSERT_TRUE(saved.has_value());

    fixture.plugin->gain = 1.0F;
    ASSERT_TRUE(fixture.instance->restore_state(saved.value()).has_value());
    EXPECT_FLOAT_EQ(fixture.plugin->gain, 0.25F);
}

TEST(JucePluginInstanceTest, ReturnsAnErrorWhenThePluginThrows) {
    Fixture fixture;
    fixture.plugin->throw_on_state = true;
    auto saved = fixture.instance->save_state();
    ASSERT_FALSE(saved.has_value());
    EXPECT_EQ(saved.error(), session::PluginStateError::kRejected);

    const std::vector<std::uint8_t> state(sizeof(float), 0);
    auto restored = fixture.instance->restore_state(state);
    ASSERT_FALSE(restored.has_value());
    EXPECT_EQ(restored.error(), session::PluginStateError::kRejected);
}

TEST(JucePluginInstanceTest, SharesTheProcessorWithTheGraph) {
    Fixture fixture;
    std::shared_ptr<audio_core::PluginProcessor> processor = fixture.instance->processor();
    ASSERT_NE(processor, nullptr);
    EXPECT_EQ(processor->layout().input_channels, 2U);
    // グラフのノードが持っている間は、インスタンスを破棄しても処理は生きている。
    fixture.instance.reset();
    EXPECT_EQ(processor->layout().output_channels, 2U);
}

}  // namespace
}  // namespace vocroz::plugin_host
