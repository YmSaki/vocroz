// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用のフェイク（FakeAudioOutput、FakeAudioInput、FakeAudioSource、FakeAudioDeviceCatalog）自体の単体テスト。
// 他のモジュールのテストがフェイクの振る舞いを前提にできるようにする。

#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_audio_device_catalog.h"
#include "fakes/fake_audio_input.h"
#include "fakes/fake_audio_output.h"
#include "fakes/fake_audio_source.h"

namespace vocroz::audio_core {
namespace {

class RecordingRender final : public AudioRenderCallback {
public:
    void render(InterleavedBuffer out) noexcept VOCROZ_NONBLOCKING override {
        frames = out.frames();
        for (float& sample : out.samples) {
            sample = 0.0F;
        }
    }
    std::size_t frames = 0;
};

class RecordingCapture final : public AudioCaptureCallback {
public:
    void on_captured(ConstInterleavedBuffer in) noexcept VOCROZ_NONBLOCKING override {
        frames += in.frames();
    }
    std::size_t frames = 0;
};

class RecordingListener final : public AudioDeviceListener {
public:
    void on_device_lost() noexcept override {
        ++lost;
    }
    int lost = 0;
};

TEST(FakesTest, OutputCallsRenderWithRequestedFrames) {
    FakeAudioOutput output(2);
    RecordingRender render;
    RecordingListener listener;
    ASSERT_TRUE(output.start(render, listener).has_value());
    EXPECT_EQ(output.render_frames(300).size(), 600U);
    EXPECT_EQ(render.frames, 300U);
    EXPECT_FALSE(output.start(render, listener).has_value());  // 二重の開始は失敗する
}

TEST(FakesTest, OutputNotifiesDeviceLostAndStopsCallingBack) {
    FakeAudioOutput output;
    RecordingRender render;
    RecordingListener listener;
    ASSERT_TRUE(output.start(render, listener).has_value());
    output.simulate_device_lost();
    EXPECT_EQ(listener.lost, 1);
    EXPECT_FALSE(output.is_started());
    EXPECT_TRUE(output.render_frames(128).empty());
    EXPECT_EQ(output.start(render, listener).error(), AudioDeviceError::kDeviceNotFound);
}

TEST(FakesTest, InputDeliversCapturedSamples) {
    FakeAudioInput input(2);
    RecordingCapture capture;
    RecordingListener listener;
    ASSERT_TRUE(input.start(capture, listener).has_value());
    const std::vector<float> samples(256, 0.5F);
    input.capture(samples);
    EXPECT_EQ(capture.frames, 128U);
    input.simulate_device_lost();
    EXPECT_EQ(listener.lost, 1);
    input.capture(samples);
    EXPECT_EQ(capture.frames, 128U);
}

TEST(FakesTest, SineSourceIsContinuousAcrossPulls) {
    FakeAudioSource joined = FakeAudioSource::sine(1.0F, 1000.0F);
    FakeAudioSource split = FakeAudioSource::sine(1.0F, 1000.0F);
    std::vector<float> whole(100);
    joined.pull(whole);
    std::vector<float> parts(100);
    split.pull(std::span<float>(parts).first(37));
    split.pull(std::span<float>(parts).subspan(37));
    EXPECT_EQ(whole, parts);
}

TEST(FakesTest, DeviceCatalogReturnsDevicesPerDirection) {
    FakeAudioDeviceCatalog catalog;
    catalog.set_devices(AudioDeviceDirection::kOutput, {AudioDeviceInfo{"out-1", "USB", 2, false}});
    catalog.set_devices(AudioDeviceDirection::kInput, {});
    const auto outputs = catalog.list(AudioDeviceDirection::kOutput);
    ASSERT_TRUE(outputs.has_value());
    ASSERT_EQ(outputs.value().size(), 1U);
    EXPECT_EQ(outputs.value()[0].id, "out-1");
    EXPECT_TRUE(catalog.list(AudioDeviceDirection::kInput).value().empty());
    catalog.set_error(AudioDeviceError::kInternal);
    EXPECT_EQ(catalog.list(AudioDeviceDirection::kOutput).error(), AudioDeviceError::kInternal);
}

}  // namespace
}  // namespace vocroz::audio_core
