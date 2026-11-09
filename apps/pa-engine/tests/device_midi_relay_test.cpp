// SPDX-License-Identifier: AGPL-3.0-only
//
// DeviceMidiRelay の単体テスト。メッセージスレッドからの開閉の通知が、ループのスレッドで
// グラフの MIDI ソースに反映されることを確かめる。

#include "device_midi_relay.h"

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/audio_core/mixer.h"

#include "engine_constants.h"
#include "fakes/fake_logger.h"

namespace vocroz::engine {
namespace {

namespace ac = audio_core;

// 時計のない 1 ブロック（イベントはすべてブロックの先頭に置かれる）。
ac::MidiBlockClock block_clock() {
    ac::MidiBlockClock clock;
    clock.frames = ac::kBlockSize;
    return clock;
}

// Note On（ノート 60、ベロシティ 100）。
ac::TimestampedMidiMessage note_on_message() {
    ac::TimestampedMidiMessage message;
    message.data = {0x90, 60, 100};
    message.size = 3;
    return message;
}
using std::chrono_literals::operator""ms;

class DeviceMidiRelayTest : public ::testing::Test {
protected:
    DeviceMidiRelayTest()
        : mixer_(ac::MixerConfig{kMixerFaderCapacity, kMixerMeterCapacity}),
          engine_mixer_(mixer_, logger_),
          relay_(loop_, engine_mixer_) {}

    // ループに積まれた処理を、このスレッドで実行し終える。
    void drain() {
        loop_.post([this] { loop_.request_stop(); });
        loop_.run(1000ms, [] {});
    }

    // 1 回のコールバックぶん描画する。グラフにつながった MIDI ソースは、ここで取り出される。
    void render() {
        std::vector<float> out(ac::kBlockSize * 2, 0.0F);
        mixer_.render(ac::InterleavedBuffer{out, 2});
        engine_mixer_.collect();
    }

    // キューに Note On を 1 つ積む。
    static void push_note(ac::MidiInputQueue& queue) {
        ASSERT_TRUE(queue.push(note_on_message()));
    }

    // キューが空なら true（グラフが取り出した）。
    static bool drained(ac::MidiInputQueue& queue) {
        std::vector<ac::MidiEvent> events(4);
        return queue.pull(block_clock(), events).count == 0;
    }

    common::FakeLogger logger_;
    ac::Mixer mixer_;
    EngineMixer engine_mixer_;
    ControlLoop loop_;
    DeviceMidiRelay relay_;
};

TEST_F(DeviceMidiRelayTest, OpenedDeviceBecomesAGraphSourceOnTheLoop) {
    auto queue = std::make_shared<ac::MidiInputQueue>();
    std::thread message_thread([&] { relay_.on_midi_device_opened("a", queue); });
    message_thread.join();
    // ループで反映するまでは、グラフにない。
    push_note(*queue);
    render();
    EXPECT_FALSE(drained(*queue));

    drain();
    push_note(*queue);
    render();
    EXPECT_TRUE(drained(*queue));
}

TEST_F(DeviceMidiRelayTest, ClosedDeviceLeavesTheGraph) {
    auto queue = std::make_shared<ac::MidiInputQueue>();
    relay_.on_midi_device_opened("a", queue);
    relay_.on_midi_device_closed("a");
    drain();
    push_note(*queue);
    render();
    EXPECT_FALSE(drained(*queue));
}

}  // namespace
}  // namespace vocroz::engine
