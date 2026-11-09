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
    float take_mic_peak() {
        return capture_.take_input_peak();
    }

    SendWakeup wakeup_;
    PacketSender sender_{wakeup_};
    RecordingOpener opener_;
    RecordingOpener app_audio_;
    common::FakeLogger logger_;
    ClientCapture capture_{
        ClientCapture::Ports{
            .sender = sender_, .microphones = opener_, .app_audio = app_audio_, .logger = logger_},
        {},
        transport::OpusFrameEncoder::create({}).value()};
};

using control_protocol::InputSourceKind;

TEST_F(ClientCaptureTest, OpensSelectedDeviceAndStartsCapture) {
    capture_.select_input("mic-1");
    EXPECT_EQ(opener_.opened, std::vector<std::string>{"mic-1"});
    EXPECT_TRUE(capture_.capturing());

    std::vector<float> block(128, 0.3F);
    opener_.last->capture(block);
    EXPECT_FLOAT_EQ(take_mic_peak(), 0.3F);
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

// --- 入力の構成（docs/adr/0035-client-input-expansion.md） ---

TEST_F(ClientCaptureTest, StartsWithAMonoMicrophoneOnStreamZero) {
    const auto* mic = capture_.processor(kMicrophoneLane);
    ASSERT_NE(mic, nullptr);
    EXPECT_EQ(mic->channels(), 1U);
    EXPECT_EQ(mic->stream_id(), 0);
    EXPECT_EQ(capture_.processor(kAppAudioLane), nullptr);
}

TEST_F(ClientCaptureTest, SwitchingTheMicrophoneToStereoReopensTheDevice) {
    capture_.select_input("mic-1");
    capture_.set_streams({{0, InputSourceKind::kMicrophone, 2}});
    EXPECT_EQ(capture_.processor(kMicrophoneLane)->channels(), 2U);
    EXPECT_EQ(opener_.opened, (std::vector<std::string>{"mic-1", "mic-1"}));
    EXPECT_TRUE(capture_.capturing());

    // 構成が変わらなければ開き直さない。
    capture_.set_streams({{0, InputSourceKind::kMicrophone, 2}});
    EXPECT_EQ(opener_.opened.size(), 2U);
}

TEST_F(ClientCaptureTest, AppAudioOpensTheLoopbackAndStopsWhenRemoved) {
    capture_.select_input("mic-1");
    capture_.set_streams(
        {{0, InputSourceKind::kMicrophone, 1}, {1, InputSourceKind::kApplicationAudio, 2}});
    EXPECT_EQ(app_audio_.opened, std::vector<std::string>{""});
    EXPECT_TRUE(capture_.capturing_app_audio());
    EXPECT_EQ(capture_.processor(kAppAudioLane)->stream_id(), 1);
    EXPECT_EQ(capture_.processor(kAppAudioLane)->channels(), 2U);
    // マイクは開き直さない。
    EXPECT_EQ(opener_.opened.size(), 1U);

    capture_.set_streams({{0, InputSourceKind::kMicrophone, 1}});
    EXPECT_FALSE(capture_.capturing_app_audio());
    EXPECT_EQ(capture_.processor(kAppAudioLane), nullptr);
    EXPECT_TRUE(capture_.capturing());
}

TEST_F(ClientCaptureTest, AppAudioThatCannotBeOpenedIsNotSentAndTheMicrophoneKeepsGoing) {
    app_audio_.missing[""] = true;
    capture_.select_input("mic-1");
    capture_.set_streams(
        {{0, InputSourceKind::kMicrophone, 1}, {1, InputSourceKind::kApplicationAudio, 2}});
    EXPECT_FALSE(capture_.capturing_app_audio());
    EXPECT_TRUE(capture_.capturing());
}

TEST_F(ClientCaptureTest, ReopensTheAppAudioOnceAfterItsDeviceIsLost) {
    capture_.set_streams(
        {{0, InputSourceKind::kMicrophone, 1}, {1, InputSourceKind::kApplicationAudio, 2}});
    app_audio_.last->simulate_device_lost();
    // アプリ音声の抜去は、マイクの抜去としては知らせない。
    EXPECT_FALSE(capture_.take_device_lost());
    capture_.flush_params();
    EXPECT_EQ(app_audio_.opened.size(), 2U);
    EXPECT_TRUE(capture_.capturing_app_audio());
    capture_.flush_params();
    EXPECT_EQ(app_audio_.opened.size(), 2U);
}

}  // namespace
}  // namespace vocroz::client
