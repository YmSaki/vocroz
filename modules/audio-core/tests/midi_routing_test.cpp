// SPDX-License-Identifier: AGPL-3.0-only
//
// MIDI の経路（GraphDescription::midi_connections）の単体テスト。記述の検証、プラグインへの配り方、
// 複数ソースの合わせ方、Panic、グラフの差し替えで外れた経路の Note Off、Mixer での
// タイムスタンプの変換と、切断で音残り（Stuck Note）がないことを検証する。

#include <array>
#include <chrono>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/audio_core/constants.h"
#include "vocroz/audio_core/midi_input_queue.h"
#include "vocroz/audio_core/midi_message.h"
#include "vocroz/audio_core/mixer.h"

#include "active_notes.h"
#include "fakes/fake_clock.h"
#include "fakes/fake_midi_source.h"
#include "fakes/fake_plugin_processor.h"
#include "graph_exchange.h"
#include "render_graph.h"

namespace vocroz::audio_core {
namespace {

constexpr PluginBusLayout kInstrument{0, 2};

MidiEvent note_on(std::uint8_t note, std::uint32_t offset = 0, std::uint8_t channel = 0) {
    const std::array<std::uint8_t, 3> bytes{static_cast<std::uint8_t>(0x90 | channel), note, 100};
    return make_midi_event(bytes, offset);
}

struct Rig {
    std::shared_ptr<FakeMidiSource> keys = std::make_shared<FakeMidiSource>();
    std::shared_ptr<FakeMidiSource> pads = std::make_shared<FakeMidiSource>();
    std::shared_ptr<FakePluginProcessor> synth = std::make_shared<FakePluginProcessor>(kInstrument);
    FaderBank faders{1, 1};
    MeterBank meters{1};
};

// ソース 1（keys）と 2（pads）を、VSTi のノード 1 へつなぐ。
GraphDescription instrument_graph(Rig& rig, bool with_keys = true, bool with_pads = true) {
    GraphDescription graph;
    graph.nodes = {NodeSpec::make_plugin(NodeId{1}, rig.synth)};
    graph.midi_sources = {{MidiSourceId{1}, rig.keys}, {MidiSourceId{2}, rig.pads}};
    if (with_keys) {
        graph.midi_connections.push_back({MidiSourceId{1}, NodeId{1}});
    }
    if (with_pads) {
        graph.midi_connections.push_back({MidiSourceId{2}, NodeId{1}});
    }
    return graph;
}

std::unique_ptr<RenderGraph> build(const GraphDescription& description) {
    auto graph = RenderGraph::build(description, 1, 1);
    EXPECT_TRUE(graph.has_value());
    return graph ? std::move(graph).value() : nullptr;
}

GraphError build_error(const GraphDescription& description) {
    auto graph = RenderGraph::build(description, 1, 1);
    EXPECT_FALSE(graph.has_value());
    return graph ? GraphError::kCycle : graph.error();
}

// 発音中のまま残ったノートを数える（受け取ったイベントを順に追跡する）。
bool has_stuck_notes(const FakePluginProcessor& plugin) {
    ActiveNotes notes;
    for (const ReceivedMidiEvent& received : plugin.received_midi()) {
        notes.observe(received.event);
    }
    return notes.any();
}

TEST(MidiRoutingTest, RejectsInvalidMidiDescriptions) {
    Rig rig;
    GraphDescription duplicate = instrument_graph(rig);
    duplicate.midi_sources.push_back({MidiSourceId{1}, rig.keys});
    EXPECT_EQ(build_error(duplicate), GraphError::kDuplicateMidiSource);

    GraphDescription missing = instrument_graph(rig);
    missing.midi_sources[0].source = nullptr;
    EXPECT_EQ(build_error(missing), GraphError::kMissingMidiSource);

    GraphDescription unknown = instrument_graph(rig);
    unknown.midi_connections.push_back({MidiSourceId{9}, NodeId{1}});
    EXPECT_EQ(build_error(unknown), GraphError::kInvalidMidiConnection);

    GraphDescription to_non_plugin = instrument_graph(rig);
    to_non_plugin.nodes.push_back(NodeSpec::make_limiter(NodeId{2}));
    to_non_plugin.midi_connections.push_back({MidiSourceId{1}, NodeId{2}});
    EXPECT_EQ(build_error(to_non_plugin), GraphError::kInvalidMidiConnection);

    GraphDescription repeated = instrument_graph(rig);
    repeated.midi_connections.push_back({MidiSourceId{1}, NodeId{1}});
    EXPECT_EQ(build_error(repeated), GraphError::kInvalidMidiConnection);
}

TEST(MidiRoutingTest, MergesSourcesInSampleOrder) {
    Rig rig;
    auto graph = build(instrument_graph(rig));
    rig.keys->queue(note_on(60, 10));
    rig.keys->queue(note_on(62, 50));
    rig.pads->queue(note_on(36, 10));
    rig.pads->queue(note_on(38, 30));
    (void)graph->process(kBlockSize, rig.faders, rig.meters);
    const auto& received = rig.synth->received_midi();
    ASSERT_EQ(received.size(), 4U);
    // 同じ位置（10）は先に記述した経路（keys）を先にする。
    EXPECT_EQ(received[0].event.data[1], 60);
    EXPECT_EQ(received[1].event.data[1], 36);
    EXPECT_EQ(received[2].event.data[1], 38);
    EXPECT_EQ(received[3].event.data[1], 62);
}

TEST(MidiRoutingTest, OneSourceFeedsSeveralPluginsWithSinglePull) {
    Rig rig;
    auto second = std::make_shared<FakePluginProcessor>(kInstrument);
    GraphDescription description = instrument_graph(rig, true, false);
    description.nodes.push_back(NodeSpec::make_plugin(NodeId{2}, second));
    description.midi_connections.push_back({MidiSourceId{1}, NodeId{2}});
    auto graph = build(description);
    rig.keys->queue(note_on(60));
    (void)graph->process(kBlockSize, rig.faders, rig.meters);
    EXPECT_EQ(rig.keys->pull_count(), 1U);
    EXPECT_EQ(rig.synth->received_midi().size(), 1U);
    EXPECT_EQ(second->received_midi().size(), 1U);
}

TEST(MidiRoutingTest, ClampsOutOfRangeOffsetsFromSource) {
    Rig rig;
    auto graph = build(instrument_graph(rig, true, false));
    rig.keys->queue(note_on(60, 100));
    rig.keys->queue(note_on(61, 20));    // 逆順
    rig.keys->queue(note_on(62, 1000));  // ブロックの外
    (void)graph->process(64, rig.faders, rig.meters);
    const auto& received = rig.synth->received_midi();
    ASSERT_EQ(received.size(), 3U);
    EXPECT_EQ(received[0].event.sample_offset, 63U);
    EXPECT_EQ(received[1].event.sample_offset, 63U);
    EXPECT_EQ(received[2].event.sample_offset, 63U);
}

TEST(MidiRoutingTest, PanicReleasesNotesOfThatSourceOnly) {
    Rig rig;
    auto graph = build(instrument_graph(rig));
    rig.keys->queue(note_on(60));
    rig.pads->queue(note_on(36));
    (void)graph->process(kBlockSize, rig.faders, rig.meters);
    rig.keys->request_panic();
    (void)graph->process(kBlockSize, rig.faders, rig.meters);
    const auto& received = rig.synth->received_midi();
    ASSERT_EQ(received.size(), 3U);
    EXPECT_EQ(received[2].call, 1U);
    EXPECT_TRUE(is_note_off(received[2].event));
    EXPECT_EQ(received[2].event.data[1], 60);
    EXPECT_EQ(received[2].event.sample_offset, 0U);
}

TEST(MidiRoutingTest, PanicNoteOffsComeBeforeNewEventsInSameBlock) {
    Rig rig;
    auto graph = build(instrument_graph(rig, true, false));
    rig.keys->queue(note_on(60));
    (void)graph->process(kBlockSize, rig.faders, rig.meters);
    rig.keys->request_panic();
    rig.keys->queue(note_on(60));  // 再接続直後の新しい打鍵
    (void)graph->process(kBlockSize, rig.faders, rig.meters);
    const auto& received = rig.synth->received_midi();
    ASSERT_EQ(received.size(), 3U);
    EXPECT_TRUE(is_note_off(received[1].event));
    EXPECT_TRUE(is_note_on(received[2].event));
}

TEST(MidiRoutingTest, RemovingRouteOnGraphSwapReleasesItsNotes) {
    Rig rig;
    GraphExchange exchange;
    exchange.publish(build(instrument_graph(rig)));
    RenderGraph* graph = exchange.acquire();
    rig.keys->queue(note_on(60));
    rig.pads->queue(note_on(36));
    (void)graph->process(kBlockSize, rig.faders, rig.meters);

    // keys の経路を外す。pads の経路は続くので、36 は鳴り続ける。
    exchange.publish(build(instrument_graph(rig, false, true)));
    graph = exchange.acquire();
    (void)graph->process(kBlockSize, rig.faders, rig.meters);
    const auto& received = rig.synth->received_midi();
    ASSERT_EQ(received.size(), 3U);
    EXPECT_TRUE(is_note_off(received[2].event));
    EXPECT_EQ(received[2].event.data[1], 60);

    // 経路が残った pads の状態は引き継がれ、その後の Panic で止まる。
    rig.pads->request_panic();
    (void)graph->process(kBlockSize, rig.faders, rig.meters);
    EXPECT_FALSE(has_stuck_notes(*rig.synth));
    exchange.collect();
}

TEST(MidiRoutingTest, KeptRouteDoesNotReleaseOnGraphSwap) {
    Rig rig;
    GraphExchange exchange;
    exchange.publish(build(instrument_graph(rig)));
    RenderGraph* graph = exchange.acquire();
    rig.keys->queue(note_on(60));
    (void)graph->process(kBlockSize, rig.faders, rig.meters);
    exchange.publish(build(instrument_graph(rig)));
    graph = exchange.acquire();
    (void)graph->process(kBlockSize, rig.faders, rig.meters);
    EXPECT_EQ(rig.synth->received_midi().size(), 1U);
    EXPECT_TRUE(has_stuck_notes(*rig.synth));
    exchange.collect();
}

// 子機の MIDI を MidiInputQueue で受け、Mixer で VSTi を鳴らす。子機が切断（LOST）したら
// session が request_panic() を呼ぶ。発音中のノートもペダルも、すべて止まること。
TEST(MidiRoutingTest, DisconnectLeavesNoStuckNotesThroughMixer) {
    common::FakeClock clock;
    clock.advance(std::chrono::seconds(1));
    Mixer mixer({}, clock);
    auto queue = std::make_shared<MidiInputQueue>();
    auto synth = std::make_shared<FakePluginProcessor>(kInstrument);
    GraphDescription description;
    description.nodes = {NodeSpec::make_plugin(NodeId{1}, synth)};
    description.midi_sources = {{MidiSourceId{1}, queue}};
    description.midi_connections = {{MidiSourceId{1}, NodeId{1}}};
    ASSERT_TRUE(mixer.apply_graph(description).has_value());

    std::vector<float> samples(kBlockSize * 2);
    const InterleavedBuffer out{samples, 2};
    auto push = [&](std::uint8_t status, std::uint8_t data1, std::uint8_t data2) {
        TimestampedMidiMessage m;
        m.timestamp = clock.now();
        m.data = {status, data1, data2};
        m.size = 3;
        ASSERT_TRUE(queue->push(m));
    };
    push(0xB0, kMidiSustainController, 127);
    push(0x90, 60, 100);
    push(0x90, 64, 100);
    push(0x80, 60, 0);
    mixer.render(out);
    EXPECT_TRUE(has_stuck_notes(*synth));

    queue->request_panic();
    clock.advance(std::chrono::milliseconds(3));
    mixer.render(out);
    EXPECT_FALSE(has_stuck_notes(*synth));
    mixer.collect_garbage();
}

// Mixer がコールバックの先頭の時刻を読み、タイムスタンプの間隔をサンプル位置の間隔として保つ。
TEST(MidiRoutingTest, MixerConvertsTimestampsAcrossSplitBlocks) {
    common::FakeClock clock;
    clock.advance(std::chrono::seconds(1));
    Mixer mixer({}, clock);
    // コールバック長（256 サンプル）ぶん先に鳴らし、タイミングの揺れをなくす構成。
    auto queue = std::make_shared<MidiInputQueue>(MidiInputQueueConfig{2 * kBlockSize});
    auto synth = std::make_shared<FakePluginProcessor>(kInstrument);
    GraphDescription description;
    description.nodes = {NodeSpec::make_plugin(NodeId{1}, synth)};
    description.midi_sources = {{MidiSourceId{1}, queue}};
    description.midi_connections = {{MidiSourceId{1}, NodeId{1}}};
    ASSERT_TRUE(mixer.apply_graph(description).has_value());

    std::vector<float> samples(2 * kBlockSize * 2);
    const InterleavedBuffer out{samples, 2};
    mixer.render(out);  // 時刻 1s、サンプル 0〜255
    // 次のコールバック（時刻 1s + 256 サンプル分）の 4ms 前と 1ms 前に打鍵する。
    const auto callback = std::chrono::microseconds(256'000'000 / kSampleRate);
    auto push_note = [&](std::chrono::microseconds before, std::uint8_t note) {
        TimestampedMidiMessage m;
        m.timestamp = clock.now() + callback - before;
        m.data = {0x90, note, 100};
        m.size = 3;
        ASSERT_TRUE(queue->push(m));
    };
    push_note(std::chrono::microseconds(4000), 60);
    push_note(std::chrono::microseconds(1000), 62);
    clock.advance(callback);
    mixer.render(
        out);  // サンプル 256〜511。打鍵はサンプル 256 + 256 - 192 = 320 と 256 + 256 - 48 = 464
    const auto& received = synth->received_midi();
    ASSERT_EQ(received.size(), 2U);
    // 1 コールバックを 2 ブロックに分けて処理する（process の 3 回目と 4 回目）。
    EXPECT_EQ(received[0].call, 2U);
    EXPECT_EQ(received[0].event.sample_offset, 320U - 256U);
    EXPECT_EQ(received[1].call, 3U);
    EXPECT_EQ(received[1].event.sample_offset, 464U - 384U);
    mixer.collect_garbage();
}

}  // namespace
}  // namespace vocroz::audio_core
