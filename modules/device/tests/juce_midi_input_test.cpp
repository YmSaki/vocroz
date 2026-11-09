// SPDX-License-Identifier: AGPL-3.0-only
//
// 実際の JUCE の MidiInput を呼ぶテスト。CI には MIDI デバイスがないので、ないポートを開かずに
// kDeviceNotFound
// を返すことと、一覧を取得できることを確かめる。実機での受信と抜去の確認は手動で行う。

#include <gtest/gtest.h>
#include <juce_events/juce_events.h>

#include "vocroz/device/factories.h"

#include "fakes/fake_clock.h"
#include "fakes/fake_logger.h"

namespace vocroz::device {
namespace {

// JUCE の MIDI はメッセージスレッドで動くので、アプリと同じく JUCE を初期化し、テストのスレッドを
// メッセージスレッドにする。
class JuceMidiInputTest : public ::testing::Test {
private:
    juce::ScopedJuceInitialiser_GUI juce_;
};

class NullMidi final : public audio_core::MidiInputCallback {
public:
    void on_midi_message(const audio_core::TimestampedMidiMessage& /*message*/) noexcept override {}
};

class NullListener final : public audio_core::AudioDeviceListener {
public:
    void on_device_lost() noexcept override {}
};

TEST_F(JuceMidiInputTest, CatalogListsPorts) {
    auto catalog = make_juce_midi_device_catalog();
    EXPECT_TRUE(catalog->list().has_value());
}

TEST_F(JuceMidiInputTest, UnknownPortIsNotFoundAndNotOpened) {
    common::FakeClock clock;
    common::FakeLogger logger;
    auto input = make_juce_midi_input("vocroz-test-no-such-port", clock, logger);
    NullMidi midi;
    NullListener listener;
    EXPECT_EQ(input->start(midi, listener).error(), audio_core::AudioDeviceError::kDeviceNotFound);
    input->stop();  // 開始していなくても呼べる
}

}  // namespace
}  // namespace vocroz::device
