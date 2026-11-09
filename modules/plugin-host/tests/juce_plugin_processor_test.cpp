// SPDX-License-Identifier: AGPL-3.0-only
//
// JucePluginProcessor の単体テスト。JUCE の AudioPluginInstance をまねたフェイクで、
// チャンネル構成と遅延の報告、グラフのバッファをその場で処理すること、
// プラグインの例外で失敗を返し続けること、MIDI の上限を超えたら止める側のイベントを残すことを
// 検証する。

#include "juce/juce_plugin_processor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/audio_core/midi_message.h"

#include "plugin_host_constants.h"

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
    int last_note_offs = 0;
    // 受け取った MIDI の (サンプル位置, バイト列)。
    std::vector<std::pair<int, std::vector<std::uint8_t>>> last_midi;

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
        last_note_offs = 0;
        last_midi.clear();
        for (const juce::MidiMessageMetadata metadata : midi) {
            const juce::MidiMessage message = metadata.getMessage();
            if (message.isNoteOff()) {
                ++last_note_offs;
            }
            last_midi.emplace_back(
                metadata.samplePosition,
                std::vector<std::uint8_t>(metadata.data, metadata.data + metadata.numBytes));
        }
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

// MidiBuffer の末尾に直接書き足す形式が JUCE の形式と合っていることを、JUCE の読み出しで確かめる。
// JUCE を上げて内部の形式が変わったら、このテストが落ちる。
TEST(JucePluginProcessorTest, PluginReadsBackTheSameMidiEventsInOrder) {
    Fixture fixture(0, 2, 0);
    const std::array events{
        audio_core::MidiEvent{0, {0xC0, 5, 0}, 2}, audio_core::MidiEvent{0, {0x90, 60, 100}, 3},
        audio_core::MidiEvent{17, {0xB0, 64, 127}, 3}, audio_core::MidiEvent{17, {0x80, 60, 0}, 3},
        audio_core::MidiEvent{63, {0xE0, 0, 64}, 3}};
    EXPECT_EQ(fixture.process(events), audio_core::PluginProcessStatus::kOk);
    const auto& received = fixture.instance->last_midi;
    ASSERT_EQ(received.size(), events.size());
    for (std::size_t i = 0; i < events.size(); ++i) {
        EXPECT_EQ(received[i].first, static_cast<int>(events[i].sample_offset)) << i;
        EXPECT_EQ(received[i].second,
                  std::vector<std::uint8_t>(events[i].data.begin(),
                                            events[i].data.begin() + events[i].size))
            << i;
    }
}

TEST(JucePluginProcessorTest, SortsEventsThatArriveOutOfOrder) {
    Fixture fixture(0, 2, 0);
    // 昇順の約束から外れても、プラグインにはサンプル位置の順に渡す。
    const std::array events{audio_core::MidiEvent{40, {0x90, 60, 100}, 3},
                            audio_core::MidiEvent{10, {0x90, 62, 100}, 3}};
    EXPECT_EQ(fixture.process(events), audio_core::PluginProcessStatus::kOk);
    const auto& received = fixture.instance->last_midi;
    ASSERT_EQ(received.size(), 2U);
    EXPECT_EQ(received[0].first, 10);
    EXPECT_EQ(received[1].first, 40);
}

TEST(JucePluginProcessorTest, ClearsMidiBetweenBlocks) {
    Fixture fixture(0, 2, 0);
    const std::array first{audio_core::MidiEvent{50, {0x90, 60, 100}, 3}};
    EXPECT_EQ(fixture.process(first), audio_core::PluginProcessStatus::kOk);
    // 前のブロックの位置より前のイベントも、次のブロックでは先頭から積む。
    const std::array second{audio_core::MidiEvent{3, {0x80, 60, 0}, 3}};
    EXPECT_EQ(fixture.process(second), audio_core::PluginProcessStatus::kOk);
    ASSERT_EQ(fixture.instance->last_midi.size(), 1U);
    EXPECT_EQ(fixture.instance->last_midi[0].first, 3);
}

TEST(JucePluginProcessorTest, KeepsReleaseEventsWhenMidiExceedsTheLimit) {
    Fixture fixture(0, 2, 0);
    // 上限より多い Note On の後に、Panic の Note Off が続くブロック。
    std::vector<audio_core::MidiEvent> events;
    const std::size_t note_ons = kMaxMidiEventsPerProcess;
    for (std::size_t i = 0; i < note_ons; ++i) {
        events.push_back(
            audio_core::MidiEvent{0, {0x90, static_cast<std::uint8_t>(i % 128), 100}, 3});
    }
    constexpr int kNoteOffs = 2064;
    for (int i = 0; i < kNoteOffs; ++i) {
        events.push_back(audio_core::make_note_off(static_cast<std::uint8_t>(i / 128),
                                                   static_cast<std::uint8_t>(i % 128), 10));
    }
    EXPECT_EQ(fixture.process(events), audio_core::PluginProcessStatus::kOk);
    EXPECT_EQ(fixture.instance->last_midi_events, static_cast<int>(kMaxMidiEventsPerProcess));
    EXPECT_EQ(fixture.instance->last_note_offs, kNoteOffs);
}

TEST(JucePluginProcessorTest, KeepsOrderOfEventsAtTheSamePositionWhenTrimming) {
    Fixture fixture(0, 2, 0);
    // 同じ位置の Note On と Note Off（ごく短い音）の順序を、絞り込みで入れ替えない。
    std::vector<audio_core::MidiEvent> events;
    for (std::size_t i = 0; i < kMaxMidiEventsPerProcess / 2 + 1; ++i) {
        events.push_back(audio_core::MidiEvent{5, {0x90, 60, 100}, 3});
        events.push_back(audio_core::make_note_off(0, 60, 5));
    }
    EXPECT_EQ(fixture.process(events), audio_core::PluginProcessStatus::kOk);
    const auto& received = fixture.instance->last_midi;
    ASSERT_FALSE(received.empty());
    EXPECT_EQ(received.front().second[0], 0x90);
    // 最後に残る Note On の後には、それを止める Note Off がある。
    EXPECT_EQ(received.back().second[0], 0x80);
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
