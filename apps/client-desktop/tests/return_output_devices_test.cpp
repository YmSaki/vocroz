// SPDX-License-Identifier: AGPL-3.0-only
//
// 返送モニターの出力デバイス（ReturnOutputDevices）のテスト。一覧、開いた出力に返送の再生を
// つなぐこと、開けなければ別のデバイスへ切り替えないこと、開き直す前に閉じることを、
// フェイクのデバイスで確かめる。

#include "return_output_devices.h"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_audio_device_catalog.h"
#include "fakes/fake_logger.h"
#include "fakes/fake_output_device_opener.h"

namespace vocroz::client {
namespace {

namespace ac = audio_core;
namespace cp = control_protocol;

class ReturnOutputDevicesTest : public ::testing::Test {
protected:
    ReturnOutputDevicesTest() {
        catalog_.set_devices(ac::AudioDeviceDirection::kOutput,
                             {ac::AudioDeviceInfo{.id = "spk-1", .name = "Headphones"},
                              ac::AudioDeviceInfo{.id = "spk-2", .name = "Speakers"}});
        opener_.available = {"spk-1", "spk-2"};
    }

    ac::FakeAudioDeviceCatalog catalog_;
    FakeOutputDeviceOpener opener_;
    std::unique_ptr<transport::PlayoutStream> returns_ =
        transport::PlayoutStream::create(transport::PlayoutConfig{}).value();
    ReturnPlayer player_{*returns_};
    common::FakeLogger logger_;
    ReturnOutputDevices outputs_{catalog_, opener_, player_, logger_};
};

TEST_F(ReturnOutputDevicesTest, ListsOutputDevices) {
    const auto devices = outputs_.list_outputs();
    ASSERT_EQ(devices.size(), 2U);
    EXPECT_EQ(devices[0], (cp::OutputDevice{"spk-1", "Headphones"}));
}

TEST_F(ReturnOutputDevicesTest, OpensTheDeviceAndRendersTheReturn) {
    const auto opened = outputs_.open_output("spk-2");
    ASSERT_TRUE(opened);
    EXPECT_EQ(opened.value(), "Speakers");
    ASSERT_NE(opener_.last, nullptr);
    // 返送が届いていないので、すべてのチャンネルに無音を書く。
    for (const float sample : opener_.last->render_frames(64)) {
        EXPECT_EQ(sample, 0.0F);
    }
}

TEST_F(ReturnOutputDevicesTest, DoesNotFallBackWhenTheDeviceCannotOpen) {
    ASSERT_TRUE(outputs_.open_output("spk-1"));
    opener_.available.erase("spk-2");
    const auto opened = outputs_.open_output("spk-2");
    ASSERT_FALSE(opened);
    EXPECT_EQ(opened.error(), cp::ErrorCode::kDeviceUnavailable);
    // 前のデバイスは閉じ、ほかのデバイスは開かない。
    EXPECT_EQ(opener_.opened, (std::vector<std::string>{"spk-1", "spk-2"}));
}

TEST_F(ReturnOutputDevicesTest, ReportsTheLossOfTheOpenDevice) {
    ASSERT_TRUE(outputs_.open_output("spk-1"));
    opener_.last->simulate_device_lost();
    EXPECT_TRUE(player_.take_device_lost());
}

}  // namespace
}  // namespace vocroz::client
