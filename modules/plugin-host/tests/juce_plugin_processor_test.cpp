// SPDX-License-Identifier: AGPL-3.0-only
//
// JucePluginProcessor の単体テスト。JUCE の AudioPluginInstance をまねたフェイクで、
// チャンネル構成と遅延の報告、グラフのバッファをその場で処理すること、
// プラグインの例外で失敗を返し続けることを検証する。

#include "juce/juce_plugin_processor.h"

#include <array>
#include <memory>
#include <span>
#include <stdexcept>

#include <gtest/gtest.h>

namespace vocroz::plugin_host {
namespace {

// 出力にゲインを掛けるだけのプラグイン。throw_on_process で processBlock() が例外を送出する。
class FakeInstance final : public juce::AudioPluginInstance {
public:
    FakeInstance(int inputs, int outputs, int latency)
        : AudioPluginInstance(make_buses(inputs, outputs)) {
        setLatencySamples(latency);
    }

    bool throw_on_process = false;
    float gain = 0.5F;
    int process_count = 0;
    int last_midi_events = 0;

    void fillInPluginDescription(juce::PluginDescription& description) const override {
        description.name = "Fake";
    }
    const juce::String getName() const override {
        return "Fake";
    }
    void prepareToPlay(double /*rate*/, int /*block*/) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        ++process_count;
        last_midi_events = midi.getNumEvents();
        if (throw_on_process) {
            throw std::runtime_error("plugin failure");
        }
        buffer.applyGain(gain);
    }
    double getTailLengthSeconds() const override {
        return 0.0;
    }
    bool acceptsMidi() const override {
        return true;
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
    void getStateInformation(juce::MemoryBlock& /*data*/) override {}
    void setStateInformation(const void* /*data*/, int /*size*/) override {}

private:
    static BusesProperties make_buses(int inputs, int outputs) {
        const auto set = [](int channels) {
            return channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        };
        BusesProperties buses;
        if (inputs > 0) {
            buses = buses.withInput("Input", set(inputs), true);
        }
        return buses.withOutput("Output", set(outputs), true);
    }
};

struct Fixture {
    explicit Fixture(int inputs = 2, int outputs = 2, int latency = 0) {
        auto owned = std::make_unique<FakeInstance>(inputs, outputs, latency);
        instance = owned.get();
        processor = std::make_unique<JucePluginProcessor>(std::move(owned));
        left.fill(1.0F);
        right.fill(1.0F);
    }

    audio_core::PluginProcessStatus process(std::span<const audio_core::MidiEvent> midi = {}) {
        audio_core::PluginProcessContext context;
        context.channels = std::span<float* const>(channels.data(), channels.size());
        context.frames = left.size();
        context.midi = midi;
        return processor->process(context);
    }

    FakeInstance* instance = nullptr;
    std::unique_ptr<JucePluginProcessor> processor;
    std::array<float, 64> left{};
    std::array<float, 64> right{};
    std::array<float*, 2> channels{left.data(), right.data()};
};

TEST(JucePluginProcessorTest, ReportsMainBusLayoutAndLatency) {
    Fixture stereo(2, 2, 64);
    EXPECT_EQ(stereo.processor->layout().input_channels, 2U);
    EXPECT_EQ(stereo.processor->layout().output_channels, 2U);
    EXPECT_EQ(stereo.processor->latency_samples(), 64U);

    Fixture instrument(0, 2, 0);
    EXPECT_EQ(instrument.processor->layout().input_channels, 0U);
    EXPECT_EQ(instrument.processor->layout().output_channels, 2U);
}

TEST(JucePluginProcessorTest, ProcessesGraphBuffersInPlace) {
    Fixture fixture;
    EXPECT_EQ(fixture.process(), audio_core::PluginProcessStatus::kOk);
    EXPECT_FLOAT_EQ(fixture.left[0], 0.5F);
    EXPECT_FLOAT_EQ(fixture.right[63], 0.5F);
}

TEST(JucePluginProcessorTest, PassesMidiEventsToPlugin) {
    Fixture fixture(0, 2, 0);
    const std::array events{audio_core::MidiEvent{0, {0x90, 60, 100}, 3},
                            audio_core::MidiEvent{32, {0x80, 60, 0}, 3}};
    EXPECT_EQ(fixture.process(events), audio_core::PluginProcessStatus::kOk);
    EXPECT_EQ(fixture.instance->last_midi_events, 2);
}

TEST(JucePluginProcessorTest, KeepsFailingAfterPluginThrows) {
    Fixture fixture;
    fixture.instance->throw_on_process = true;
    EXPECT_EQ(fixture.process(), audio_core::PluginProcessStatus::kFailed);
    fixture.instance->throw_on_process = false;
    // 状態が壊れているおそれがあるので、例外の後はプラグインを呼ばない。
    EXPECT_EQ(fixture.process(), audio_core::PluginProcessStatus::kFailed);
    EXPECT_EQ(fixture.instance->process_count, 1);
}

}  // namespace
}  // namespace vocroz::plugin_host
