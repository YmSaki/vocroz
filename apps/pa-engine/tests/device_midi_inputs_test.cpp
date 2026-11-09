// SPDX-License-Identifier: AGPL-3.0-only
//
// DeviceMidiInputs の単体テスト。フェイクの一覧とポートで、開き方、届いたメッセージの積み方、
// 抜去と一覧から消えたときの Panic と通知、開けなかったポートの扱いを確かめる。

#include "device_midi_inputs.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/audio_core/constants.h"

#include "fakes/fake_logger.h"
#include "fakes/fake_midi_device_catalog.h"
#include "fakes/fake_midi_input.h"

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

class RecordingListener final : public DeviceMidiListener {
public:
    void on_midi_device_opened(const std::string& device_id,
                               std::shared_ptr<ac::MidiInputQueue> queue) override {
        log.push_back("opened:" + device_id);
        queues[device_id] = std::move(queue);
    }
    void on_midi_device_closed(const std::string& device_id) override {
        log.push_back("closed:" + device_id);
    }

    std::vector<std::string> log;
    std::map<std::string, std::shared_ptr<ac::MidiInputQueue>> queues;
};

class DeviceMidiInputsTest : public ::testing::Test {
protected:
    DeviceMidiInputsTest()
        : inputs_(DeviceMidiInputs::Ports{
              catalog_,
              [this](const ac::MidiDeviceInfo& info) -> std::unique_ptr<ac::MidiInput> {
                  auto input = std::make_unique<ac::FakeMidiInput>();
                  if (busy_ == info.id) {
                      // start() を失敗させる（抜去済みのフェイクは開始できない）。
                      input->simulate_device_lost();
                  }
                  inputs[info.id] = input.get();
                  ++opened;
                  return input;
              },
              listener_, logger_}) {}

    static ac::MidiPullResult pull(ac::MidiInputQueue& queue, std::vector<ac::MidiEvent>& events) {
        events.assign(16, ac::MidiEvent{});
        const auto result = queue.pull(block_clock(), events);
        events.resize(result.count);
        return result;
    }

    ac::FakeMidiDeviceCatalog catalog_;
    RecordingListener listener_;
    common::FakeLogger logger_;
    std::string busy_;
    std::map<std::string, ac::FakeMidiInput*> inputs;
    int opened = 0;
    DeviceMidiInputs inputs_;
};

TEST_F(DeviceMidiInputsTest, OpensEveryListedPortAndQueuesItsMessages) {
    catalog_.set_devices({{"a", "Keyboard"}, {"b", "Pads"}});
    inputs_.refresh();
    EXPECT_EQ(inputs_.open_count(), 2U);
    EXPECT_EQ(listener_.log, (std::vector<std::string>{"opened:a", "opened:b"}));
    ASSERT_TRUE(inputs["a"]->started());

    inputs["a"]->receive(note_on_message());
    std::vector<ac::MidiEvent> events;
    pull(*listener_.queues["a"], events);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].data[0], 0x90);
    pull(*listener_.queues["b"], events);
    EXPECT_TRUE(events.empty());
}

TEST_F(DeviceMidiInputsTest, RefreshDoesNotReopenPortsAlreadyOpen) {
    catalog_.set_devices({{"a", "Keyboard"}});
    inputs_.refresh();
    inputs_.refresh();
    EXPECT_EQ(opened, 1);
    EXPECT_EQ(listener_.log, (std::vector<std::string>{"opened:a"}));
}

TEST_F(DeviceMidiInputsTest, UnpluggedPortRequestsPanicAndIsReopenedWhenItReturns) {
    catalog_.set_devices({{"a", "Keyboard"}});
    inputs_.refresh();
    auto first = listener_.queues["a"];
    inputs["a"]->receive(note_on_message());
    inputs["a"]->simulate_device_lost();
    EXPECT_EQ(listener_.log, (std::vector<std::string>{"opened:a", "closed:a"}));
    std::vector<ac::MidiEvent> events;
    EXPECT_TRUE(pull(*first, events).panic);

    // 抜いたポートが一覧から消え、また戻った。
    catalog_.set_devices({});
    inputs_.refresh();
    EXPECT_EQ(inputs_.open_count(), 0U);
    catalog_.set_devices({{"a", "Keyboard"}});
    inputs_.refresh();
    EXPECT_EQ(inputs_.open_count(), 1U);
    EXPECT_EQ(listener_.log.back(), "opened:a");
    EXPECT_NE(listener_.queues["a"], first);
}

TEST_F(DeviceMidiInputsTest, PortThatLeavesTheListWithoutNoticeIsClosedWithPanic) {
    catalog_.set_devices({{"a", "Keyboard"}});
    inputs_.refresh();
    auto queue = listener_.queues["a"];
    catalog_.set_devices({});
    inputs_.refresh();
    EXPECT_EQ(listener_.log.back(), "closed:a");
    EXPECT_EQ(inputs_.open_count(), 0U);
    std::vector<ac::MidiEvent> events;
    EXPECT_TRUE(pull(*queue, events).panic);
}

TEST_F(DeviceMidiInputsTest, PortThatCannotBeOpenedIsRetriedOnlyAfterItLeavesTheList) {
    busy_ = "a";
    catalog_.set_devices({{"a", "Keyboard"}});
    inputs_.refresh();
    inputs_.refresh();
    EXPECT_EQ(opened, 1);
    EXPECT_EQ(inputs_.open_count(), 0U);
    EXPECT_TRUE(listener_.log.empty());

    busy_.clear();
    catalog_.set_devices({});
    inputs_.refresh();
    catalog_.set_devices({{"a", "Keyboard"}});
    inputs_.refresh();
    EXPECT_EQ(inputs_.open_count(), 1U);
}

TEST_F(DeviceMidiInputsTest, ListFailureKeepsOpenPorts) {
    catalog_.set_devices({{"a", "Keyboard"}});
    inputs_.refresh();
    catalog_.set_error(ac::AudioDeviceError::kInternal);
    inputs_.refresh();
    EXPECT_EQ(inputs_.open_count(), 1U);
}

TEST_F(DeviceMidiInputsTest, CloseAllStopsEveryPortWithPanic) {
    catalog_.set_devices({{"a", "Keyboard"}});
    inputs_.refresh();
    auto queue = listener_.queues["a"];
    inputs_.close_all();
    EXPECT_EQ(inputs_.open_count(), 0U);
    std::vector<ac::MidiEvent> events;
    EXPECT_TRUE(pull(*queue, events).panic);
}

}  // namespace
}  // namespace vocroz::engine
