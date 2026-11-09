// SPDX-License-Identifier: AGPL-3.0-only
//
// session の CaptureControl の実装（ClientCapture）のテスト。入力デバイスの選び直しで開き直すこと、
// 開けなければ入力なしのままにすること（別のデバイスへ切り替えない）、抜去を一度だけ知らせることを
// 確かめる。

#include "client_capture.h"

#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_audio_input.h"
#include "fakes/fake_logger.h"

namespace vocroz::client {
namespace {

// 開いた入力を記録し、テストから参照できるようにする。
class RecordingOpener final : public InputDeviceOpener {
public:
    [[nodiscard]] common::Result<std::unique_ptr<audio_core::AudioInput>,
                                 audio_core::AudioDeviceError>
    open(std::string_view device_id) override {
        opened.emplace_back(device_id);
        if (missing.contains(std::string(device_id))) {
            return common::make_unexpected(audio_core::AudioDeviceError::kDeviceNotFound);
        }
        auto input = std::make_unique<audio_core::FakeAudioInput>();
        last = input.get();
        return std::unique_ptr<audio_core::AudioInput>(std::move(input));
    }

    std::vector<std::string> opened;
    std::map<std::string, bool> missing;
    audio_core::FakeAudioInput* last = nullptr;
};

class ClientCaptureTest : public ::testing::Test {
protected:
    SendWakeup wakeup_;
    PacketSender sender_{wakeup_};
    CaptureProcessor processor_{transport::OpusFrameEncoder::create({}).value(), sender_};
    RecordingOpener opener_;
    common::FakeLogger logger_;
    ClientCapture capture_{processor_, opener_, logger_};
};

TEST_F(ClientCaptureTest, OpensSelectedDeviceAndStartsCapture) {
    capture_.select_input("mic-1");
    EXPECT_EQ(opener_.opened, std::vector<std::string>{"mic-1"});
    EXPECT_TRUE(capture_.capturing());

    std::vector<float> block(128, 0.3F);
    opener_.last->capture(block);
    EXPECT_FLOAT_EQ(processor_.take_input_peak(), 0.3F);
}

TEST_F(ClientCaptureTest, NoInputStopsCapture) {
    capture_.select_input("mic-1");
    capture_.select_input(std::nullopt);
    EXPECT_FALSE(capture_.capturing());
}

TEST_F(ClientCaptureTest, StaysWithoutInputWhenDeviceCannotBeOpened) {
    opener_.missing["gone"] = true;
    capture_.select_input("gone");
    EXPECT_FALSE(capture_.capturing());
    // 別のデバイスは開かない。
    EXPECT_EQ(opener_.opened, std::vector<std::string>{"gone"});
}

TEST_F(ClientCaptureTest, ReportsDeviceLossOnce) {
    capture_.select_input("mic-1");
    opener_.last->simulate_device_lost();
    EXPECT_TRUE(capture_.take_device_lost());
    EXPECT_FALSE(capture_.take_device_lost());
}

}  // namespace
}  // namespace vocroz::client
